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
| `sms.xml` | `<smses count backup_set backup_date type="archive">` | `<sms>` and `<mms>` elements |
| `calls.xml` | `<calls count backup_set backup_date type="archive">` | `<call>` elements |
| `sms-<yyyyMMddHHmmss>.xml` | `<smses count backup_set backup_date type="full">` | `<sms>` and `<mms>` elements |
| `calls-<yyyyMMddHHmmss>.xml` | `<calls count backup_set backup_date type="full">` | `<call>` elements |

* **Incremental backups** (the default) add to `sms.xml` and `calls.xml`, the way the
  "archive mode" of the Android app does: what is in the file stays exactly as it is (all
  attributes, also the ones this app does not use), the phone's entries that are not in it
  yet are added at the end, and `count` and `backup_date` are updated. Entries compare as
  described under [Duplicates](#duplicates); entries that cannot be restored (drafts,
  reports, ...) compare by their own fields. A file without anything new is not rewritten.
  The new file replaces the old one only once it is complete. A file that cannot be read
  is left alone and the backup fails with a message. Archive files of the Android app can
  be continued, and the Android app can continue files of this app.
* **New files** each time: both files of one run share the time stamp and the
  `backup_set` id, like the files of the Android app. The app lists them as one backup.
* `backup_date` and all `date` attributes are milliseconds since 1970 (UTC). Sailfish OS
  stores seconds, so milliseconds are lost when a backup goes through Sailfish OS.
* Values the app does not know are written as `null`, like the Android app does.
* Compressed backups (`.xml.zip`, the XML file inside) can be restored. Password-protected
  zip files cannot; unzip them on a computer first.
* The Android app writes emoji as two UTF-16 surrogate references (`&#55357;&#56832;`)
  and sometimes control characters (`&#1;`). Both are invalid XML; they are repaired while
  reading. This app writes emoji as UTF-8, so its files are valid XML. Entries copied into
  an archive file are written the same way.

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

An `<mms>` holds `<parts>` (SMIL layout, texts, pictures and other attachments, base64 in
`data`) and `<addrs>` (From 137, To 151, Cc 130, Bcc 129). Sailfish OS keeps an MMS as an
event of type MMS with its parts in files (`MessageParts` table, files in
`~/.local/share/commhistory/data/<event id>/`) and the To/Cc/Bcc lists in its headers.

| Attribute | Backup (Sailfish OS → file) | Restore (file → Sailfish OS) |
| --- | --- | --- |
| `msg_box` | 1 received, 2 sent, 3 draft, 4 outbox, 5 failed | 1 and 2 are restored; drafts and unsent MMS are left out |
| `m_type` | 132 received, 128 sent | notifications of never downloaded MMS (130), delivery and read reports (134–136) are left out |
| `date` | end time (received/sent), ms | end time |
| `date_sent` | received MMS: start time (sent time), **seconds**, like Android; sent MMS: 0 | start time of received MMS (seconds or ms) |
| `address` | the conversation's members without the owner, joined with `~` | the conversation's members; if missing or an RCS group chat id, taken from `<addrs>` |
| `sub` | subject | subject |
| `read` | 1/0 | read flag, or "read" for everything (default) |
| `text_only` | 1 if there are no attachments | not used (the parts tell) |
| `m_id`, `tr_id` | the ids the MMS had in the restored backup, else `null` | kept with the event (event properties `smscallbackup-m_id`, `smscallbackup-tr_id`), so a backup made on Sailfish OS restores into the same message on Android |
| `sub_id` | SIM slot | not used |
| `readable_date`, `contact_name` | as for SMS | not used |
| others (`ct_t`, `seen`, `m_size`, ...) | typical values | not used |

Parts: `seq="-1"` is the SMIL layout (`text` holds it), `text/plain` parts have `text` and
`chset="106"`, attachments have `data` (base64), `ct`, `cl` (file name) and `cid`.

* Restored MMS with attachments get their SMIL, text and attachment parts as files, like
  the MMS the phone receives itself; the message text is the text parts joined by line
  breaks (as commhistoryd does it). File names are made safe (only `- . 0-9 a-z A-Z`).
  The files are written to a folder next to the database while the backup is read and
  moved into `data/<event id>/` once the message exists (commhistoryd removes folders
  there that belong to no message).
* MMS with text only (most RCS chats) are restored without files: the text is the
  message.
* Backups the Android app made without media (option "media" off) have no `data`: such
  attachments are counted and left out; the rest of the message is restored.
* MMS sent from a Sailfish OS phone and the To/Cc lists of received ones are backed up as
  they are.

### Group messages

A group MMS (several members in `address` or `<addrs>`) goes into a conversation with all
its members, like the ones the Messages app makes when a message is sent to several
people: received messages are filed under their sender (shown when the message is
tapped), and a reply goes to everyone as separate messages. Members that are the owner's
own numbers are left out: Android does not list the owner in `address`; when only `<addrs>`
names the members, the owner's numbers are taken from one-to-one MMS in the same backup
(their single To).

### RCS

Google Messages keeps RCS chat messages as MMS (often `ct_t="text/plain"`, a `text/plain`
part named `body`, `tr_id` starting with `proto:`), and the Android app backs them up as
`<mms>`. They are restored like other MMS:

* one-to-one chats go into the conversation with that number;
* group chats, whose `address` is a chat id ending in `@rcs.google.com`, get their members
  from `<addrs>` (see above);
* business chats (`…@bot.rcs.google.com`) are filed under the business name from
  `contact_name`, like an SMS from an alphanumeric sender.

Sailfish OS has no RCS, so a backup made on Sailfish OS writes these messages as MMS. RCS
chats Samsung phones keep as SMS (`<sms s_chat="1">`) are restored as SMS.

## Duplicates

Before restoring, every entry is compared with the history on the phone:

* messages by type, direction, the time they were received or sent (seconds), the
  number (last 8 digits; the sender of received group messages, the first recipient of
  sent ones) and the text (for MMS: the trimmed text and the subject);
* calls by direction, start time, number, duration and whether they were missed (the
  ringing time of missed calls is not compared, it is not in Android backups).

Entries that match are not restored again, neither are entries that appear twice in the
selected files. Restoring the same backup twice therefore adds nothing the second time.

## Writing to the phone

Everything is written with libcommhistory (`GroupManager::addGroups`,
`EventModel::addEvents`, for the parts of MMS `EventModel::modifyEvents`), the library
commhistoryd and the Phone app use. It announces the
changes on D-Bus, so the Messages and Phone apps show the restored history at once and
commhistoryd files new incoming messages into the right conversations. The rows look like
the ones the phone writes itself: received SMS get a UUID message token and
`lastModified = 0`, sent SMS a `/ril_N/message_<hex>` token, calls `lastModified` = start
time, MMS no token and their headers and parts, and every event the SIM's
`subscriberIdentity` (copied from the newest event of that SIM). The database is only read
directly (with a read-only SQLite connection), to make backups and to find duplicates.

## Automatic backups

The app installs a systemd user timer, `smscallbackup-scheduled.timer`, that starts
`smscallbackup --scheduled` every 15 minutes, but only while automatic backups are on
(the service has `ConditionPathExists=%h/.config/de.jzab/smscallbackup/schedule-active`,
a file the app writes and removes). That command backs up with the app's settings if a
backup time (daily, weekly on a day, monthly on a day; months without that day use their
last day) has passed since the last automatic backup, or since the schedule was changed.
A backup the phone missed because it was switched off or asleep therefore runs at the
first check after that. The result is written to the app's settings, which the app shows;
a failure also shows a notification. Backups of the app and the automatic backup never
run at the same time (a `flock()` on `~/.cache/de.jzab/smscallbackup/backup.lock`).
