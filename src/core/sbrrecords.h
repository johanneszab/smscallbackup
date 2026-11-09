// SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
// SPDX-License-Identifier: MIT

#ifndef SBRRECORDS_H
#define SBRRECORDS_H

#include <QList>
#include <QPair>
#include <QString>

// Records of the XML backup format of the Android app "SMS Backup & Restore" (SyncTech).
// Field meanings follow https://www.synctech.com.au/sms-backup-restore/fields-in-xml-backup-files/
// (see docs/FORMAT.md). Attribute values "null" are read as null strings, and null strings
// are written as "null", the way the Android app does it.
namespace Sbr {

// <sms type="..."> and <mms msg_box="...">: Android's Telephony MESSAGE_TYPE_* values
enum MessageBox {
    BoxReceived = 1,
    BoxSent = 2,
    BoxDraft = 3,
    BoxOutbox = 4,
    BoxFailed = 5,
    BoxQueued = 6
};

// <sms status="...">
enum SmsStatus {
    SmsStatusNone = -1,
    SmsStatusComplete = 0,   // delivery report received
    SmsStatusPending = 32,
    SmsStatusFailed = 64
};

// <call type="...">: Android's CallLog.Calls.*_TYPE values
enum CallType {
    CallIncoming = 1,
    CallOutgoing = 2,
    CallMissed = 3,
    CallVoicemail = 4,
    CallRejected = 5,
    CallBlocked = 6,
    CallAnsweredExternally = 7
};

// <call presentation="...">
enum Presentation {
    PresentationAllowed = 1,
    PresentationRestricted = 2,
    PresentationUnknown = 3,
    PresentationPayphone = 4
};

// <addr type="...">: MMS PDU address types
enum MmsAddressType {
    AddrBcc = 129,
    AddrCc = 130,
    AddrFrom = 137,
    AddrTo = 151
};

// <mms m_type="...">: MMS PDU types
enum MmsPduType {
    MmsSendRequest = 128,        // sent message
    MmsNotificationInd = 130,    // notification of a message that was never downloaded
    MmsRetrieveConf = 132,       // received message
    MmsDeliveryInd = 134,        // delivery report
    MmsReadRecInd = 135,         // read report (sent)
    MmsReadOrigInd = 136         // read report (received)
};

// Placeholder for the phone's own number in <addr> (the From of sent MMS).
const char *const OwnAddressToken = "insert-address-token";
// MIBenum of UTF-8, the usual chset/charset value.
const int CharsetUtf8 = 106;

// Attributes in file order, values as written ("null" stays "null"). Records keep all of
// them, also the ones this app does not use, so an archive backup can copy entries it does
// not change exactly.
typedef QList<QPair<QString, QString> > AttributeList;

struct SmsRecord
{
    int protocol = 0;
    QString address;
    qint64 date = 0;             // ms since the epoch: when the message was received or sent
    int type = 0;                // MessageBox
    QString subject;             // usually null
    QString body;
    QString toa;                 // usually null
    QString scToa;               // usually null
    QString serviceCenter;       // usually null
    int read = 1;
    int status = SmsStatusNone;
    int locked = 0;
    qint64 dateSent = 0;         // ms since the epoch, 0 if unknown
    QString subId;               // Android subscription id as written, "-1" if unknown
    QString readableDate;        // optional
    QString contactName;         // optional, "(Unknown)" for numbers without a contact
    AttributeList attributes;    // as read from a file; empty for records made by this app
};

struct CallRecord
{
    QString number;
    qint64 duration = 0;         // seconds
    qint64 date = 0;             // ms since the epoch
    int type = 0;                // CallType
    int presentation = PresentationAllowed;
    QString subscriptionId;      // null if unknown
    QString postDialDigits;      // e.g. ",123" for an extension, usually empty
    QString subscriptionComponentName; // null if unknown
    QString readableDate;        // optional
    QString contactName;         // optional
    AttributeList attributes;    // as read from a file; empty for records made by this app
};

// MMS are kept close to the file: their attribute set depends on the phone.
struct MmsPart
{
    AttributeList attributes;    // seq, ct, name, chset, cd, fn, cid, cl, ctt_s, ctt_t, text, data, ...
    QString value(const QString &name) const;
};

struct MmsAddress
{
    QString address;
    int type = 0;                // MmsAddressType
    int charset = 106;
};

struct MmsRecord
{
    AttributeList attributes;    // date, msg_box, m_type, address, sub, read, ...
    QList<MmsPart> parts;
    QList<MmsAddress> addresses;
    QString value(const QString &name) const;
    // A number attribute; missing, "null" or not a number give the fallback.
    qint64 number(const QString &name, qint64 fallback) const;
};

// Reads an attribute list value; missing attributes and "null" give a null string.
QString attributeValue(const AttributeList &attributes, const QString &name);
// Sets an attribute, adding it at the end if it is not there yet.
void setAttribute(AttributeList *attributes, const QString &name, const QString &value);

} // namespace Sbr

#endif // SBRRECORDS_H
