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
    SkipReasonCount
};

struct ImportOptions
{
    // Old messages would otherwise show up as new: by default everything is marked read.
    bool markAsRead = true;
};

// Android timestamps are milliseconds; very old backups used seconds.
qint64 secondsFromBackupTime(qint64 value);

SkipReason smsToEvent(const Sbr::SmsRecord &sms, const ImportOptions &options, HistoryEvent *event);
SkipReason callToEvent(const Sbr::CallRecord &call, const ImportOptions &options, HistoryEvent *event);

// contactName: the contact's name, or a null string for "(Unknown)".
Sbr::SmsRecord eventToSms(const HistoryEvent &event, const QString &contactName);
Sbr::CallRecord eventToCall(const HistoryEvent &event, const QString &contactName);

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

QString skipReasonText(SkipReason reason);

} // namespace Mapping

#endif // MAPPING_H
