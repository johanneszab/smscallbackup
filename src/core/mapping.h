// SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
// SPDX-License-Identifier: MIT

#ifndef MAPPING_H
#define MAPPING_H

#include "historyevent.h"
#include "sbrrecords.h"

#include <QByteArray>
#include <QString>

// Conversions between the records of an SMS Backup & Restore file and Sailfish OS history
// events. Both directions are lossless for everything the two systems have in common;
// docs/FORMAT.md lists what maps to what.
namespace Mapping {

enum SkipReason {
    NotSkipped = 0,
    SkipDraft,              // drafts are not part of the history on Sailfish OS
    SkipUnsent,             // outbox, queued or failed messages
    SkipNoAddress,
    SkipNoDate,
    SkipUnknownType,
    SkipMmsReport,          // MMS notifications of messages never downloaded, delivery/read reports
    SkipNoContent,          // MMS with neither text nor attachments (e.g. left out of the backup)
    SkipReasonCount
};

struct ImportOptions
{
    // Old messages would otherwise show up as new: by default everything is marked read.
    bool markAsRead = true;
    // Decode MMS attachments. Not needed to compare entries.
    bool withPartData = true;
};

// Android timestamps are milliseconds; very old backups used seconds.
qint64 secondsFromBackupTime(qint64 value);

SkipReason smsToEvent(const Sbr::SmsRecord &sms, const ImportOptions &options, HistoryEvent *event);
SkipReason callToEvent(const Sbr::CallRecord &call, const ImportOptions &options, HistoryEvent *event);
// MMS, and the RCS chat messages Google Messages keeps as MMS. Messages with attachments
// get parts (with data and a file name); text-only ones only freeText, like an SMS.
// missingAttachments counts attachments the backup does not contain (backups made with
// "media" switched off).
SkipReason mmsToEvent(const Sbr::MmsRecord &mms, const ImportOptions &options, HistoryEvent *event,
                      int *missingAttachments = nullptr);
// For a received one-to-one MMS: the address it was sent to, which is one of the phone
// owner's numbers. Empty if the record does not tell. Used to leave the owner out of
// group conversations whose members only the <addr> list names.
QString ownAddressOf(const Sbr::MmsRecord &mms);

// contactName: the contact's name, or a null string for "(Unknown)".
Sbr::SmsRecord eventToSms(const HistoryEvent &event, const QString &contactName);
Sbr::CallRecord eventToCall(const HistoryEvent &event, const QString &contactName);
// The parts' data must be loaded; without parts, freeText becomes a text part.
Sbr::MmsRecord eventToMms(const HistoryEvent &event, const QString &contactName);

// "Nov 28, 2008 18:16:12" in local time, like the readable_date of the Android app.
QString readableDate(qint64 msecs);

// SIM slot of a cellular account: 1 for .../ring/tel/ril_0, 2 for ril_1; 0 if unknown.
int simSlot(const QString &localUid);
QString ringAccount(int slot);

// Identity of an event, used to skip what is already on the phone. Messages compare by
// type, direction, the time they were received or sent, the address and the text; calls
// by direction, start time, address, duration and whether they were missed. Addresses
// compare the way the Messages app does (last digits of the number).
QByteArray duplicateKey(const HistoryEvent &event);

// Identity of a backup entry, used to add only new entries to an archive file. Entries
// that can be restored compare like duplicateKey(), others (drafts, reports, ...) by
// their own fields. Records made from phone events with eventToSms() & co. have the key
// of the entry they become.
QByteArray archiveKey(const Sbr::SmsRecord &record);
QByteArray archiveKey(const Sbr::CallRecord &record);
QByteArray archiveKey(const Sbr::MmsRecord &record);

// A file name for an MMS part, made safe the way commhistoryd does it (only - . 0-9 a-z
// A-Z); `index` numbers parts that have no name.
QString partFileName(const QString &name, const QString &contentType, int index);
// Decodes a text part with the charset of its content type (default UTF-8).
QString decodeText(const QByteArray &data, const QString &contentType);

QString skipReasonText(SkipReason reason);

} // namespace Mapping

#endif // MAPPING_H
