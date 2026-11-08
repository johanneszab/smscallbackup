// SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
// SPDX-License-Identifier: MIT

#include "mapping.h"

#include "phonenumber.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QLocale>

namespace {

// A received message's sent time (date_sent) is only used this close to its receive time.
const qint64 MaxDeliveryDelay = 7 * 24 * 3600;
const QLatin1String RingAccountPrefix("/org/freedesktop/Telepathy/Account/ring/tel/");
const QLatin1String RcsBotDomain("@bot.rcs.google.com");
const QLatin1String UnknownContact("(Unknown)");
const QLatin1String Rejected("rejected");

QString emptyIfNull(const QString &text)
{
    return text.isNull() ? QString(QLatin1String("")) : text;
}

} // namespace

namespace Mapping {

qint64 secondsFromBackupTime(qint64 value)
{
    if (value <= 0)
        return 0;
    return value > Q_INT64_C(100000000000) ? value / 1000 : value;
}

SkipReason smsToEvent(const Sbr::SmsRecord &sms, const ImportOptions &options, HistoryEvent *event)
{
    switch (sms.type) {
    case Sbr::BoxReceived:
    case Sbr::BoxSent:
        break;
    case Sbr::BoxDraft:
        return SkipDraft;
    case Sbr::BoxOutbox:
    case Sbr::BoxFailed:
    case Sbr::BoxQueued:
        return SkipUnsent;
    default:
        return SkipUnknownType;
    }
    const qint64 end = secondsFromBackupTime(sms.date);
    if (end <= 0)
        return SkipNoDate;
    QString address = sms.address.trimmed();
    if (address.isEmpty())
        return SkipNoAddress;
    // RCS business chats have no phone number: file them under the business name, the
    // way Sailfish OS shows an SMS from an alphanumeric sender.
    if (address.endsWith(RcsBotDomain, Qt::CaseInsensitive)) {
        const QString name = sms.contactName.trimmed();
        if (!name.isEmpty() && name != UnknownContact)
            address = name;
    }

    HistoryEvent e;
    e.type = HistoryEvent::SmsEvent;
    e.direction = sms.type == Sbr::BoxReceived ? HistoryEvent::Inbound : HistoryEvent::Outbound;
    e.startTime = end;
    e.endTime = end;
    e.remoteUid = address;
    e.freeText = emptyIfNull(sms.body);
    if (e.direction == HistoryEvent::Inbound) {
        // Like commhistoryd: a received message starts when it was sent, ends when it arrived.
        const qint64 sent = secondsFromBackupTime(sms.dateSent);
        if (sent > 0 && sent <= end && end - sent <= MaxDeliveryDelay)
            e.startTime = sent;
        e.isRead = options.markAsRead || sms.read != 0;
        e.status = HistoryEvent::UnknownStatus;
    } else {
        e.isRead = true;
        e.status = sms.status == Sbr::SmsStatusComplete ? HistoryEvent::DeliveredStatus
                                                       : HistoryEvent::SentStatus;
    }
    *event = e;
    return NotSkipped;
}

SkipReason callToEvent(const Sbr::CallRecord &call, const ImportOptions &options, HistoryEvent *event)
{
    Q_UNUSED(options)
    int direction = HistoryEvent::Inbound;
    bool missed = false;
    QString incomingStatus;
    switch (call.type) {
    case Sbr::CallIncoming:
    case Sbr::CallAnsweredExternally:
        break;
    case Sbr::CallOutgoing:
        direction = HistoryEvent::Outbound;
        break;
    case Sbr::CallMissed:
    case Sbr::CallVoicemail:
        missed = true;
        break;
    case Sbr::CallRejected:
    case Sbr::CallBlocked:
        missed = true;
        incomingStatus = Rejected;
        break;
    default:
        return SkipUnknownType;
    }
    const qint64 start = secondsFromBackupTime(call.date);
    if (start <= 0)
        return SkipNoDate;

    QString number = call.number.trimmed();
    if (call.presentation != Sbr::PresentationAllowed || number == QLatin1String("-1")
            || number == QLatin1String("-2") || number == QLatin1String("-3")) {
        number.clear();     // withheld, unknown or payphone: the Phone app shows "Private number"
    } else if (!number.isEmpty() && !call.postDialDigits.isEmpty() && !number.endsWith(call.postDialDigits)) {
        number += call.postDialDigits;
    }

    HistoryEvent e;
    e.type = HistoryEvent::CallEvent;
    e.direction = direction;
    e.startTime = start;
    e.endTime = missed ? start : start + qMax<qint64>(0, call.duration);
    e.isMissedCall = missed;
    e.incomingStatus = incomingStatus;
    e.remoteUid = number;
    e.isRead = true;
    *event = e;
    return NotSkipped;
}

Sbr::SmsRecord eventToSms(const HistoryEvent &e, const QString &contactName)
{
    Sbr::SmsRecord r;
    r.protocol = 0;
    r.address = emptyIfNull(e.remoteUid);
    r.date = (e.endTime > 0 ? e.endTime : e.startTime) * 1000;
    if (e.isDraft) {
        r.type = Sbr::BoxDraft;
    } else if (e.direction == HistoryEvent::Inbound) {
        r.type = Sbr::BoxReceived;
    } else {
        switch (e.status) {
        case HistoryEvent::TemporarilyFailedStatus:
        case HistoryEvent::PermanentlyFailedStatus:
        case HistoryEvent::TemporarilyFailedOfflineStatus:
            r.type = Sbr::BoxFailed;
            break;
        case HistoryEvent::SendingStatus:
        case HistoryEvent::WaitingStatus:
            r.type = Sbr::BoxOutbox;
            break;
        default:
            r.type = Sbr::BoxSent;
        }
    }
    r.body = emptyIfNull(e.freeText);
    r.read = (e.isRead || e.direction == HistoryEvent::Outbound) ? 1 : 0;
    if (r.type == Sbr::BoxSent && e.status == HistoryEvent::DeliveredStatus)
        r.status = Sbr::SmsStatusComplete;
    else if (r.type == Sbr::BoxFailed)
        r.status = Sbr::SmsStatusFailed;
    else
        r.status = Sbr::SmsStatusNone;
    r.locked = 0;
    r.dateSent = (e.direction == HistoryEvent::Inbound && e.startTime > 0) ? e.startTime * 1000 : 0;
    const int slot = simSlot(e.localUid);
    r.subId = QString::number(slot > 0 ? slot : -1);
    r.readableDate = readableDate(r.date);
    r.contactName = contactName.isEmpty() ? QString(UnknownContact) : contactName;
    return r;
}

Sbr::CallRecord eventToCall(const HistoryEvent &e, const QString &contactName)
{
    Sbr::CallRecord r;
    r.number = emptyIfNull(e.remoteUid);
    r.date = e.startTime * 1000;
    if (e.direction == HistoryEvent::Outbound) {
        r.type = Sbr::CallOutgoing;
        r.duration = e.callDuration();
    } else if (!e.isMissedCall) {
        r.type = Sbr::CallIncoming;
        r.duration = e.callDuration();
    } else {
        r.type = e.incomingStatus == Rejected ? Sbr::CallRejected : Sbr::CallMissed;
        r.duration = 0;
    }
    r.presentation = r.number.isEmpty() ? Sbr::PresentationRestricted : Sbr::PresentationAllowed;
    const int slot = simSlot(e.localUid);
    r.subscriptionId = slot > 0 ? QString::number(slot) : QString();
    r.postDialDigits = QLatin1String("");
    r.readableDate = readableDate(r.date);
    r.contactName = contactName.isEmpty() ? QString(UnknownContact) : contactName;
    return r;
}

QString readableDate(qint64 msecs)
{
    return QLocale::c().toString(QDateTime::fromMSecsSinceEpoch(msecs).toLocalTime(),
                                 QStringLiteral("MMM d, yyyy HH:mm:ss"));
}

int simSlot(const QString &localUid)
{
    if (!localUid.startsWith(RingAccountPrefix))
        return 0;
    const QString name = localUid.mid(RingAccountPrefix.size());
    if (!name.startsWith(QLatin1String("ril_")))
        return 0;
    bool ok = false;
    const int index = name.mid(4).toInt(&ok);
    return ok && index >= 0 ? index + 1 : 0;
}

QString ringAccount(int slot)
{
    return RingAccountPrefix + QStringLiteral("ril_%1").arg(qMax(slot, 1) - 1);
}

QByteArray duplicateKey(const HistoryEvent &e)
{
    QByteArray key;
    const QByteArray uid = PhoneNumber::matchKey(e.remoteUid).toUtf8();
    if (e.type == HistoryEvent::CallEvent) {
        // The ringing time of a missed call is not in Android backups: not compared.
        key = "c|" + QByteArray::number(e.direction) + '|' + QByteArray::number(e.startTime)
                + '|' + uid + '|' + QByteArray::number(e.isMissedCall ? 0 : e.callDuration())
                + '|' + (e.isMissedCall ? '1' : '0');
    } else {
        const qint64 when = e.endTime > 0 ? e.endTime : e.startTime;
        key = "m" + QByteArray::number(e.type) + '|' + QByteArray::number(e.direction) + '|'
                + QByteArray::number(when) + '|' + uid + '|'
                + QCryptographicHash::hash(e.freeText.toUtf8(), QCryptographicHash::Sha1);
    }
    return key;
}

QString skipReasonText(SkipReason reason)
{
    switch (reason) {
    case SkipDraft:
        return QCoreApplication::translate("Mapping", "drafts");
    case SkipUnsent:
        return QCoreApplication::translate("Mapping", "unsent or failed messages");
    case SkipNoAddress:
        return QCoreApplication::translate("Mapping", "entries without a phone number");
    case SkipNoDate:
        return QCoreApplication::translate("Mapping", "entries without a date");
    case SkipUnknownType:
        return QCoreApplication::translate("Mapping", "entries of an unknown type");
    default:
        return QString();
    }
}

} // namespace Mapping
