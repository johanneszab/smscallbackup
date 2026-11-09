// SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
// SPDX-License-Identifier: MIT

#include "mapping.h"

#include "phonenumber.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QLocale>
#include <QRegularExpression>
#include <QSet>
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <QTextCodec>
#endif

namespace {

// A received message's sent time (date_sent) is only used this close to its receive time.
const qint64 MaxDeliveryDelay = 7 * 24 * 3600;
const QLatin1String RingAccountPrefix("/org/freedesktop/Telepathy/Account/ring/tel/");
// RCS business chats ("verify@bot.rcs.google.com") and RCS group chats, whose id ends
// in "@rcs.google.com" or similar and is no phone number.
const QLatin1String RcsBotDomain("@bot.rcs.google.com");
const QLatin1String RcsDomain("rcs.google.com");
const QLatin1String UnknownContact("(Unknown)");
const QLatin1String Rejected("rejected");
const QLatin1String MultipartRelated("application/vnd.wap.multipart.related");

QString emptyIfNull(const QString &text)
{
    return text.isNull() ? QString(QLatin1String("")) : text;
}

QByteArray textHash(const QString &text)
{
    return QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha1);
}

bool isOwnToken(const QString &address)
{
    return address.compare(QLatin1String(Sbr::OwnAddressToken), Qt::CaseInsensitive) == 0;
}

bool isRcsBot(const QString &address)
{
    return address.endsWith(RcsBotDomain, Qt::CaseInsensitive);
}

bool isRcsGroupId(const QString &address)
{
    return address.endsWith(RcsDomain, Qt::CaseInsensitive) && !isRcsBot(address);
}

// Collects addresses once each (compared like the Messages app does), leaving out the
// owner's placeholder and RCS group chat ids.
class AddressList
{
public:
    void add(const QString &address)
    {
        const QString a = address.trimmed();
        if (a.isEmpty() || isOwnToken(a) || isRcsGroupId(a))
            return;
        const QString key = PhoneNumber::matchKey(a);
        if (m_keys.contains(key))
            return;
        m_keys.insert(key);
        m_list.append(a);
    }
    void prepend(const QString &address)
    {
        const QString a = address.trimmed();
        if (a.isEmpty() || isOwnToken(a) || isRcsGroupId(a) || m_keys.contains(PhoneNumber::matchKey(a)))
            return;
        m_keys.insert(PhoneNumber::matchKey(a));
        m_list.prepend(a);
    }
    const QStringList &list() const { return m_list; }

private:
    QStringList m_list;
    QSet<QString> m_keys;
};

// The conversation members the Android app lists in the address attribute ("a~b~c"),
// without the owner. RCS group chats have a chat id there instead.
QStringList listedAddresses(const Sbr::MmsRecord &mms)
{
    AddressList list;
    for (const QString &address : mms.value(QStringLiteral("address")).split(QLatin1Char('~')))
        list.add(address);
    return list.list();
}

QStringList addressesOfType(const Sbr::MmsRecord &mms, int type)
{
    QStringList list;
    for (const Sbr::MmsAddress &address : mms.addresses) {
        const QString a = address.address.trimmed();
        if (address.type == type && !a.isEmpty() && !isOwnToken(a))
            list.append(a);
    }
    return list;
}

QString mimeOf(const QString &contentType)
{
    return contentType.section(QLatin1Char(';'), 0, 0).trimmed().toLower();
}

QString extensionFor(const QString &mime)
{
    static const struct { const char *mime; const char *extension; } table[] = {
        { "image/jpeg", ".jpg" }, { "image/jpg", ".jpg" }, { "image/png", ".png" },
        { "image/gif", ".gif" }, { "image/bmp", ".bmp" }, { "image/webp", ".webp" },
        { "image/heic", ".heic" }, { "image/heif", ".heif" }, { "video/mp4", ".mp4" },
        { "video/3gpp", ".3gp" }, { "video/3gpp2", ".3g2" }, { "video/quicktime", ".mov" },
        { "video/webm", ".webm" }, { "audio/amr", ".amr" }, { "audio/mpeg", ".mp3" },
        { "audio/mp4", ".m4a" }, { "audio/aac", ".aac" }, { "audio/ogg", ".ogg" },
        { "audio/3gpp", ".3gp" }, { "audio/wav", ".wav" }, { "text/plain", ".txt" },
        { "text/x-vcard", ".vcf" }, { "text/vcard", ".vcf" }, { "text/x-vcalendar", ".vcs" },
        { "text/calendar", ".ics" }, { "application/smil", ".smil" }, { "application/pdf", ".pdf" },
    };
    for (const auto &entry : table) {
        if (mime == QLatin1String(entry.mime))
            return QLatin1String(entry.extension);
    }
    return QString();
}

// Text of an MMS part with base64 data instead of a text attribute; chset is a MIBenum.
QString decodeCharset(const QByteArray &data, int mibEnum)
{
    switch (mibEnum) {
    case 3:     // US-ASCII
    case 4:     // ISO-8859-1
        return QString::fromLatin1(data);
    case 1015: {    // UTF-16, big-endian unless the byte order mark says otherwise
        int start = 0;
        bool little = false;
        if (data.size() >= 2 && uchar(data.at(0)) == 0xFF && uchar(data.at(1)) == 0xFE) {
            start = 2;
            little = true;
        } else if (data.size() >= 2 && uchar(data.at(0)) == 0xFE && uchar(data.at(1)) == 0xFF) {
            start = 2;
        }
        QString text;
        for (int i = start; i + 1 < data.size(); i += 2) {
            const ushort first = uchar(data.at(i));
            const ushort second = uchar(data.at(i + 1));
            text += QChar(little ? ushort(first | (second << 8)) : ushort((first << 8) | second));
        }
        return text;
    }
    default:
        return QString::fromUtf8(data);
    }
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
    if (isRcsBot(address)) {
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

SkipReason mmsToEvent(const Sbr::MmsRecord &mms, const ImportOptions &options, HistoryEvent *event,
                      int *missingAttachments)
{
    const int box = int(mms.number(QStringLiteral("msg_box"), 0));
    switch (box) {
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
    const int pdu = int(mms.number(QStringLiteral("m_type"), 0));
    if (pdu != 0 && pdu != Sbr::MmsSendRequest && pdu != Sbr::MmsRetrieveConf)
        return SkipMmsReport;
    const qint64 end = secondsFromBackupTime(mms.number(QStringLiteral("date"), 0));
    if (end <= 0)
        return SkipNoDate;
    const bool inbound = box == Sbr::BoxReceived;

    // Who is in the conversation: the address attribute lists everyone but the owner. If
    // it is missing (older backups of RCS chats) or an RCS group chat id, the <addr>
    // list tells; a single To of a received message is the owner.
    const QStringList from = addressesOfType(mms, Sbr::AddrFrom);
    const QStringList to = addressesOfType(mms, Sbr::AddrTo);
    const QStringList cc = addressesOfType(mms, Sbr::AddrCc);
    const QStringList bcc = addressesOfType(mms, Sbr::AddrBcc);
    AddressList members;
    for (const QString &address : listedAddresses(mms))
        members.add(address);
    if (members.list().isEmpty()) {
        if (inbound) {
            for (const QString &address : from)
                members.add(address);
            if (to.size() + cc.size() > 1) {
                for (const QString &address : to + cc)
                    members.add(address);
            }
        } else {
            for (const QString &address : to + cc + bcc)
                members.add(address);
        }
    }
    QString sender;
    if (inbound) {
        sender = from.value(0);
        if (sender.isEmpty() || isRcsGroupId(sender))
            sender = members.list().value(0);
        members.prepend(sender);
    }
    QStringList participants = members.list();
    QString remoteUid = inbound ? sender.trimmed() : participants.value(0);
    if (participants.size() == 1 && isRcsBot(participants.first())) {
        // An RCS business chat: under the business name, as for SMS.
        const QString name = mms.value(QStringLiteral("contact_name")).trimmed();
        if (!name.isEmpty() && name != UnknownContact)
            remoteUid = name;
        participants.clear();
    }
    if (remoteUid.isEmpty())
        return SkipNoAddress;
    if (participants.size() < 2)
        participants.clear();

    // The text parts make the message text (like commhistoryd does it); pictures and the
    // other attachments are kept as parts with their SMIL layout. Text-only messages,
    // which is what most RCS chat messages are, need no files.
    QStringList texts;
    QList<HistoryPart> parts;
    bool haveMedia = false;
    int missing = 0;
    int index = 0;
    for (const Sbr::MmsPart &p : mms.parts) {
        ++index;
        QString contentType = p.value(QStringLiteral("ct"));
        if (contentType.isEmpty())
            contentType = QStringLiteral("application/octet-stream");
        const QString mime = mimeOf(contentType);
        QString name = p.value(QStringLiteral("cl"));
        if (name.isEmpty())
            name = p.value(QStringLiteral("name"));
        if (name.isEmpty())
            name = p.value(QStringLiteral("fn"));
        const QString data = p.value(QStringLiteral("data"));

        HistoryPart part;
        part.contentId = p.value(QStringLiteral("cid"));
        if (mime == QLatin1String("application/smil") || mime == QLatin1String("text/plain")) {
            QString text = p.value(QStringLiteral("text"));
            if (text.isNull() && !data.isNull()) {
                bool ok = false;
                const int charset = p.value(QStringLiteral("chset")).toInt(&ok);
                text = decodeCharset(QByteArray::fromBase64(data.toLatin1()), ok ? charset : Sbr::CharsetUtf8);
            }
            if (text.isNull())
                continue;
            if (mime == QLatin1String("text/plain")) {
                const QString trimmed = text.trimmed();
                if (!trimmed.isEmpty())
                    texts.append(trimmed);
                part.contentType = QStringLiteral("text/plain;charset=utf-8");
            } else {
                part.contentType = QStringLiteral("application/smil");
            }
            if (options.withPartData)
                part.data = text.toUtf8();
        } else {
            if (data.isEmpty()) {
                ++missing;      // the backup was made without attachments
                continue;
            }
            haveMedia = true;
            part.contentType = contentType;
            if (options.withPartData)
                part.data = QByteArray::fromBase64(data.toLatin1());
        }
        part.fileName = partFileName(name, part.contentType, index);
        parts.append(part);
    }
    if (missingAttachments)
        *missingAttachments += missing;
    if (!haveMedia)
        parts.clear();

    HistoryEvent e;
    e.type = HistoryEvent::MmsEvent;
    e.direction = inbound ? HistoryEvent::Inbound : HistoryEvent::Outbound;
    e.startTime = end;
    e.endTime = end;
    e.remoteUid = remoteUid;
    e.participants = participants;
    e.freeText = texts.join(QLatin1Char('\n'));
    e.subject = emptyIfNull(mms.value(QStringLiteral("sub")).trimmed());
    if (!haveMedia && e.freeText.isEmpty() && e.subject.isEmpty())
        return SkipNoContent;
    e.parts = parts;
    e.toList = to;
    e.ccList = cc;
    e.bccList = inbound ? QStringList() : bcc;
    e.mmsMessageId = mms.value(QStringLiteral("m_id"));
    e.mmsTransactionId = mms.value(QStringLiteral("tr_id"));
    if (inbound) {
        // Android keeps the sent time of MMS in seconds.
        const qint64 sent = secondsFromBackupTime(mms.number(QStringLiteral("date_sent"), 0));
        if (sent > 0 && sent <= end && end - sent <= MaxDeliveryDelay)
            e.startTime = sent;
        e.isRead = options.markAsRead || mms.number(QStringLiteral("read"), 1) != 0;
        e.status = HistoryEvent::ReceivedStatus;
    } else {
        e.isRead = true;
        e.status = HistoryEvent::SentStatus;
    }
    *event = e;
    return NotSkipped;
}

QString ownAddressOf(const Sbr::MmsRecord &mms)
{
    if (mms.number(QStringLiteral("msg_box"), 0) != Sbr::BoxReceived)
        return QString();
    const QStringList listed = listedAddresses(mms);
    const QStringList to = addressesOfType(mms, Sbr::AddrTo);
    if (listed.size() != 1 || to.size() != 1)
        return QString();
    const QString key = PhoneNumber::matchKey(to.first());
    if (key == PhoneNumber::matchKey(listed.first()))
        return QString();
    for (const QString &from : addressesOfType(mms, Sbr::AddrFrom)) {
        if (key == PhoneNumber::matchKey(from))
            return QString();
    }
    return to.first();
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

Sbr::MmsRecord eventToMms(const HistoryEvent &e, const QString &contactName)
{
    using Sbr::AttributeList;
    const bool inbound = e.direction == HistoryEvent::Inbound;
    // The box like for SMS.
    const Sbr::SmsRecord sms = eventToSms(e, contactName);

    QStringList participants = e.participants;
    if (participants.isEmpty() && !e.remoteUid.isEmpty())
        participants.append(e.remoteUid);

    Sbr::MmsRecord r;
    qint64 size = 0;
    bool haveMedia = false;
    QList<HistoryPart> parts = e.parts;
    if (parts.isEmpty() && !e.freeText.isEmpty()) {
        HistoryPart text;
        text.contentType = QStringLiteral("text/plain;charset=utf-8");
        text.contentId = QStringLiteral("<text_0>");
        text.fileName = QStringLiteral("text_0.txt");
        text.data = e.freeText.toUtf8();
        parts.append(text);
    }
    int index = 0;
    for (const HistoryPart &p : parts) {
        QString name = p.fileName;
        if (name.isEmpty())
            name = p.path.section(QLatin1Char('/'), -1);
        if (name.isEmpty())
            name = partFileName(QString(), p.contentType, index);
        ++index;
        const QString contentId = p.contentId.isEmpty() ? QString() : p.contentId;
        size += p.data.size();
        AttributeList a;
        if (p.isSmil()) {
            a << qMakePair(QStringLiteral("seq"), QStringLiteral("-1"))
              << qMakePair(QStringLiteral("ct"), QStringLiteral("application/smil"))
              << qMakePair(QStringLiteral("name"), name)
              << qMakePair(QStringLiteral("chset"), QString())
              << qMakePair(QStringLiteral("cd"), QString())
              << qMakePair(QStringLiteral("fn"), QString())
              << qMakePair(QStringLiteral("cid"), contentId)
              << qMakePair(QStringLiteral("cl"), name)
              << qMakePair(QStringLiteral("ctt_s"), QString())
              << qMakePair(QStringLiteral("ctt_t"), QString())
              << qMakePair(QStringLiteral("text"), decodeText(p.data, p.contentType));
        } else if (p.isText()) {
            a << qMakePair(QStringLiteral("seq"), QStringLiteral("0"))
              << qMakePair(QStringLiteral("ct"), QStringLiteral("text/plain"))
              << qMakePair(QStringLiteral("name"), QString())
              << qMakePair(QStringLiteral("chset"), QString::number(Sbr::CharsetUtf8))
              << qMakePair(QStringLiteral("cd"), QString())
              << qMakePair(QStringLiteral("fn"), QString())
              << qMakePair(QStringLiteral("cid"), contentId)
              << qMakePair(QStringLiteral("cl"), name)
              << qMakePair(QStringLiteral("ctt_s"), QString())
              << qMakePair(QStringLiteral("ctt_t"), QString())
              << qMakePair(QStringLiteral("text"), decodeText(p.data, p.contentType));
        } else {
            haveMedia = true;
            a << qMakePair(QStringLiteral("seq"), QStringLiteral("0"))
              << qMakePair(QStringLiteral("ct"), p.contentType)
              << qMakePair(QStringLiteral("name"), name)
              << qMakePair(QStringLiteral("chset"), QString())
              << qMakePair(QStringLiteral("cd"), QString())
              << qMakePair(QStringLiteral("fn"), QString())
              << qMakePair(QStringLiteral("cid"), contentId)
              << qMakePair(QStringLiteral("cl"), name)
              << qMakePair(QStringLiteral("ctt_s"), QString())
              << qMakePair(QStringLiteral("ctt_t"), QString())
              << qMakePair(QStringLiteral("text"), QString())
              << qMakePair(QStringLiteral("data"), QString::fromLatin1(p.data.toBase64()));
        }
        Sbr::MmsPart part;
        part.attributes = a;
        r.parts.append(part);
    }

    const QString subject = e.subject.isEmpty() ? QString() : e.subject;
    const qint64 sentSeconds = (inbound && e.startTime > 0) ? e.startTime : 0;
    const int slot = simSlot(e.localUid);
    AttributeList &a = r.attributes;
    a << qMakePair(QStringLiteral("date"), QString::number(sms.date))
      << qMakePair(QStringLiteral("ct_t"), QString(MultipartRelated))
      << qMakePair(QStringLiteral("msg_box"), QString::number(sms.type))
      << qMakePair(QStringLiteral("rr"), QString())
      << qMakePair(QStringLiteral("sub"), subject)
      << qMakePair(QStringLiteral("read_status"), QString())
      << qMakePair(QStringLiteral("seen"), QStringLiteral("1"))
      << qMakePair(QStringLiteral("sub_id"), QString::number(slot > 0 ? slot : -1))
      << qMakePair(QStringLiteral("read"), QString::number(sms.read))
      << qMakePair(QStringLiteral("m_id"), e.mmsMessageId.isEmpty() ? QString() : e.mmsMessageId)
      << qMakePair(QStringLiteral("sub_cs"), subject.isNull() ? QString() : QString::number(Sbr::CharsetUtf8))
      << qMakePair(QStringLiteral("resp_st"), QString())
      << qMakePair(QStringLiteral("date_sent"), QString::number(sentSeconds))
      << qMakePair(QStringLiteral("retr_txt_cs"), QString())
      << qMakePair(QStringLiteral("ct_cls"), QString())
      << qMakePair(QStringLiteral("m_type"), QString::number(inbound ? Sbr::MmsRetrieveConf : Sbr::MmsSendRequest))
      << qMakePair(QStringLiteral("d_rpt"), QString())
      << qMakePair(QStringLiteral("address"), participants.join(QLatin1Char('~')))
      << qMakePair(QStringLiteral("text_only"), QString::number(haveMedia ? 0 : 1))
      << qMakePair(QStringLiteral("locked"), QStringLiteral("0"))
      << qMakePair(QStringLiteral("m_size"), QString::number(size))
      << qMakePair(QStringLiteral("tr_id"), e.mmsTransactionId.isEmpty() ? QString() : e.mmsTransactionId)
      << qMakePair(QStringLiteral("readable_date"), sms.readableDate)
      << qMakePair(QStringLiteral("contact_name"), sms.contactName);

    auto addAddress = [&r](const QString &address, int type) {
        Sbr::MmsAddress entry;
        entry.address = address;
        entry.type = type;
        entry.charset = Sbr::CharsetUtf8;
        r.addresses.append(entry);
    };
    if (inbound) {
        addAddress(e.remoteUid, Sbr::AddrFrom);
        for (const QString &address : e.toList)
            addAddress(address, Sbr::AddrTo);
        for (const QString &address : e.ccList)
            addAddress(address, Sbr::AddrCc);
    } else {
        addAddress(QLatin1String(Sbr::OwnAddressToken), Sbr::AddrFrom);
        for (const QString &address : e.toList.isEmpty() ? participants : e.toList)
            addAddress(address, Sbr::AddrTo);
        for (const QString &address : e.ccList)
            addAddress(address, Sbr::AddrCc);
        for (const QString &address : e.bccList)
            addAddress(address, Sbr::AddrBcc);
    }
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
        QString text = e.freeText;
        if (e.type == HistoryEvent::MmsEvent) {
            // MMS text is made of trimmed parts; the subject tells picture messages apart.
            text = text.trimmed();
            const QString subject = e.subject.trimmed();
            if (!subject.isEmpty())
                text += QChar(0x1f) + subject;
        }
        key = "m" + QByteArray::number(e.type) + '|' + QByteArray::number(e.direction) + '|'
                + QByteArray::number(when) + '|' + uid + '|' + textHash(text);
    }
    return key;
}

QByteArray archiveKey(const Sbr::SmsRecord &r)
{
    ImportOptions options;
    options.markAsRead = false;
    HistoryEvent e;
    if (smsToEvent(r, options, &e) == NotSkipped)
        return duplicateKey(e);
    return "s" + QByteArray::number(r.type) + '|' + QByteArray::number(secondsFromBackupTime(r.date)) + '|'
            + PhoneNumber::matchKey(r.address).toUtf8() + '|' + textHash(emptyIfNull(r.body));
}

QByteArray archiveKey(const Sbr::CallRecord &r)
{
    HistoryEvent e;
    if (callToEvent(r, ImportOptions(), &e) == NotSkipped)
        return duplicateKey(e);
    return "k" + QByteArray::number(r.type) + '|' + QByteArray::number(secondsFromBackupTime(r.date)) + '|'
            + PhoneNumber::matchKey(r.number).toUtf8() + '|' + QByteArray::number(r.duration);
}

QByteArray archiveKey(const Sbr::MmsRecord &r)
{
    ImportOptions options;
    options.markAsRead = false;
    options.withPartData = false;
    HistoryEvent e;
    if (mmsToEvent(r, options, &e) == NotSkipped)
        return duplicateKey(e);
    QString text;
    for (const Sbr::MmsPart &part : r.parts) {
        if (mimeOf(part.value(QStringLiteral("ct"))) == QLatin1String("text/plain"))
            text += part.value(QStringLiteral("text")).trimmed() + QLatin1Char('\n');
    }
    return "x" + QByteArray::number(r.number(QStringLiteral("msg_box"), 0)) + '|'
            + QByteArray::number(r.number(QStringLiteral("m_type"), 0)) + '|'
            + QByteArray::number(secondsFromBackupTime(r.number(QStringLiteral("date"), 0))) + '|'
            + PhoneNumber::matchKey(r.value(QStringLiteral("address"))).toUtf8() + '|' + textHash(text);
}

QString partFileName(const QString &name, const QString &contentType, int index)
{
    QString base = name.trimmed();
    base = base.mid(qMax(base.lastIndexOf(QLatin1Char('/')), base.lastIndexOf(QLatin1Char('\\'))) + 1);
    if (base.startsWith(QLatin1Char('<')) && base.endsWith(QLatin1Char('>')))
        base = base.mid(1, base.size() - 2);
    static const QRegularExpression unsafe(QStringLiteral("[^-.0-9a-zA-Z]"));
    base.replace(unsafe, QStringLiteral("_"));
    while (base.startsWith(QLatin1Char('.')))
        base.remove(0, 1);
    const QString extension = extensionFor(mimeOf(contentType));
    if (base.isEmpty() || base.count(QLatin1Char('_')) == base.size())
        base = QStringLiteral("part%1").arg(index) + extension;
    else if (!base.contains(QLatin1Char('.')))
        base += extension;
    const int maxLength = 80;
    if (base.size() > maxLength) {
        const int dot = base.lastIndexOf(QLatin1Char('.'));
        const QString suffix = (dot > 0 && base.size() - dot <= 10) ? base.mid(dot) : QString();
        base = base.left(maxLength - suffix.size()) + suffix;
    }
    return base;
}

QString decodeText(const QByteArray &data, const QString &contentType)
{
    QString charset;
    const int pos = contentType.indexOf(QLatin1String("charset="), 0, Qt::CaseInsensitive);
    if (pos >= 0) {
        charset = contentType.mid(pos + 8).section(QLatin1Char(';'), 0, 0).trimmed();
        charset.remove(QLatin1Char('"'));
    }
    const QString lower = charset.toLower();
    if (charset.isEmpty() || lower == QLatin1String("utf-8") || lower == QLatin1String("utf8"))
        return QString::fromUtf8(data);
    if (lower == QLatin1String("us-ascii") || lower == QLatin1String("iso-8859-1") || lower == QLatin1String("latin1"))
        return QString::fromLatin1(data);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    if (QTextCodec *codec = QTextCodec::codecForName(charset.toLatin1()))
        return codec->toUnicode(data);
#endif
    return QString::fromUtf8(data);
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
    case SkipMmsReport:
        return QCoreApplication::translate("Mapping", "MMS notifications and reports");
    case SkipNoContent:
        return QCoreApplication::translate("Mapping", "MMS without text or attachments");
    default:
        return QString();
    }
}

} // namespace Mapping
