<!-- SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh -->
<!-- SPDX-License-Identifier: MIT -->

# Backup format and mapping

SMS & Call Backup reads and writes the XML files of the Android app
[SMS Backup & Restore](https://www.synctech.com.au/sms-backup-restore/) by SyncTech, as
documented in
[Fields in XML backup files](https://www.synctech.com.au/sms-backup-restore/fields-in-xml-backup-files/).
This page lists how the fields map to the Sailfish OS communication history
(libcommhistory, `~/.local/share/commhistory/commhistory.db`).

## Files

| File | Root element | Contents |
| --- | --- | --- |
| `sms-<yyyyMMddHHmmss>.xml` | `<smses count backup_set backup_date>` | `<sms>` (and `<mms>`) elements |
| `calls-<yyyyMMddHHmmss>.xml` | `<calls count backup_set backup_date>` | `<call>` elements |

* Both files of one backup run share the time stamp and the `backup_set` id, like the
  files of the Android app. The app lists them as one backup.
* `backup_date` and all `date` attributes are milliseconds since 1970 (UTC). Sailfish OS
  stores seconds, so milliseconds are lost when a backup goes through Sailfish OS.
* Values the app does not know are written as `null`, like the Android app does.
* Compressed backups (`.xml.zip`, the XML file inside) can be restored. Password-protected
  zip files cannot; unzip them on a computer first.
* The Android app writes emoji as two UTF-16 surrogate references (`&#55357;&#56832;`)
  and sometimes control characters (`&#1;`). Both are invalid XML; they are repaired while
  reading. This app writes emoji as UTF-8, so its files are valid XML.

## SMS (`<sms>`)

| Attribute | Backup (Sailfish OS → file) | Restore (file → Sailfish OS) |
| --- | --- | --- |
| `address` | remote number or sender name | remote number; RCS business chats (`…@bot.rcs.google.com`) use `contact_name` as sender |
| `date` | end time (received/sent), ms | end time |
| `date_sent` | received messages: start time (sent time from the network), ms; sent messages: 0 | start time of received messages, if it is at most 7 days before `date` |
| `type` | 1 received, 2 sent, 3 draft, 4 outbox (sending), 5 failed | 1 and 2 are restored; 3–6 (draft, outbox, failed, queued) are left out |
| `body` | message text | message text (`null` → empty) |
| `read` | 1/0 | read flag, or "read" for everything (default, can be switched off) |
| `status` | 0 delivered, 64 failed, else -1 | sent messages: 0 → delivered, else sent |
| `sub_id` | SIM slot (1 = SIM 1, 2 = SIM 2), -1 if unknown | not used; restored history goes to the SIM chosen in the app |
| `readable_date` | e.g. `Nov 28, 2008 18:16:12`, local time | not used |
| `contact_name` | the contact's name or `(Unknown)` (can be switched off) | only for RCS business chats |
| `protocol`, `subject`, `toa`, `sc_toa`, `service_center`, `locked` | `0`, `null`, `null`, `null`, `null`, `0` | not used |

Restored messages are filed into the conversation with the same number on the chosen SIM,
using the same comparison as the Messages app (the last 8 digits, so `0176…` and
`+49176…` are one conversation). New conversations are named after the international form
of the number if the backup contains it.

## Calls (`<call>`)

| Attribute | Backup | Restore |
| --- | --- | --- |
| `number` | remote number; empty for private numbers | remote number with `post_dial_digits` appended; empty for private numbers |
| `duration` | seconds (0 for missed calls) | end time = start + duration |
| `date` | start time, ms | start time |
| `type` | 1 incoming, 2 outgoing, 3 missed, 5 rejected | 1, 7 → incoming; 2 → outgoing; 3, 4 (voicemail) → missed; 5, 6 (blocked) → rejected; other types are left out |
| `presentation` | 1, or 2 (restricted) for private numbers | anything but 1, and the old numbers -1/-2/-3, give a private number |
| `subscription_id` | SIM slot, `null` if unknown | not used |
| `post_dial_digits` | empty (Sailfish OS keeps them in the number) | appended to the number |
| `subscription_component_name` | `null` | not used |
| `readable_date`, `contact_name` | as for SMS | not used |

## MMS (`<mms>`)

Not supported yet: backups contain no MMS, and MMS in a backup file are counted and left
out when restoring. The reader already parses them (`parts`, `addrs`).

## Duplicates

Before restoring, every entry is compared with the history on the phone:

* messages by type, direction, the time they were received or sent (seconds), the
  number (last 8 digits) and the text;
* calls by direction, start time, number, duration and whether they were missed (the
  ringing time of missed calls is not compared, it is not in Android backups).

Entries that match are not restored again, neither are entries that appear twice in the
selected files. Restoring the same backup twice therefore adds nothing the second time.

## Writing to the phone

Everything is written with libcommhistory (`GroupManager::addGroups`,
`EventModel::addEvents`), the library commhistoryd and the Phone app use. It announces the
changes on D-Bus, so the Messages and Phone apps show the restored history at once and
commhistoryd files new incoming messages into the right conversations. The rows look like
the ones the phone writes itself: received SMS get a UUID message token and
`lastModified = 0`, sent SMS a `/ril_N/message_<hex>` token, calls `lastModified` = start
time, and every event the SIM's `subscriberIdentity` (copied from the newest event of that
SIM). The database is only read directly (with a read-only SQLite connection), to make
backups and to find duplicates.
