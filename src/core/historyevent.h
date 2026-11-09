// SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
// SPDX-License-Identifier: MIT

#ifndef HISTORYEVENT_H
#define HISTORYEVENT_H

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

// One part of an MMS: a picture, a text, the SMIL layout, ... Sailfish OS keeps each part
// in a file (MessageParts table, files in ~/.local/share/commhistory/data/<event id>/).
struct HistoryPart
{
    QString contentId;      // e.g. "<image001>", as referenced by the SMIL layout
    QString contentType;    // e.g. "image/jpeg", "text/plain;charset=utf-8"
    QString path;           // the file on the phone (backup), or a staged file (restore)
    QString fileName;       // restore: name for the file, from the backup
    QByteArray data;        // the content, when loaded

    QString mimeType() const { return contentType.section(QLatin1Char(';'), 0, 0).trimmed().toLower(); }
    bool isText() const { return mimeType() == QLatin1String("text/plain"); }
    bool isSmil() const { return mimeType() == QLatin1String("application/smil"); }
    bool isMedia() const { return !isText() && !isSmil(); }
};

// One entry of the Sailfish OS communication history (the libcommhistory database behind
// the Messages and Phone apps), reduced to what a backup needs.
//
// The enum values are the ones of CommHistory::Event and of the Events table, so this
// struct can be filled from the database and compared without libcommhistory. That keeps
// the core code buildable and testable on a PC.
struct HistoryEvent
{
    enum Type {
        UnknownType = 0,
        SmsEvent = 2,
        CallEvent = 3,
        MmsEvent = 6
    };

    enum Direction {
        UnknownDirection = 0,
        Inbound = 1,
        Outbound = 2
    };

    enum Status {
        ManualNotificationStatus = -4,
        WaitingStatus = -3,
        DownloadingStatus = -2,
        ReceivedStatus = -1,
        UnknownStatus = 0,
        SendingStatus = 1,
        SentStatus = 2,
        DeliveredStatus = 3,
        TemporarilyFailedStatus = 4,
        PermanentlyFailedStatus = 5,
        TemporarilyFailedOfflineStatus = 6
    };

    int id = -1;
    int type = UnknownType;
    int direction = UnknownDirection;
    // Seconds since the epoch (UTC), as stored by libcommhistory. For a received message
    // startTime is when it was sent and endTime when the phone got it; a call ends at
    // endTime.
    qint64 startTime = 0;
    qint64 endTime = 0;
    QString localUid;          // Telepathy account, e.g. .../Account/ring/tel/ril_0 (SIM 1)
    QString remoteUid;         // phone number or alphanumeric sender; empty for private calls
    QString freeText;
    QString subject;
    bool isDraft = false;
    bool isRead = true;
    bool isMissedCall = false;
    int status = UnknownStatus;
    QString incomingStatus;    // event property "incomingStatus": "rejected" or "ignored"
    QString subscriberIdentity; // event property "subscriberIdentity" (the SIM's IMSI)

    // Conversations with several people (group MMS, RCS group chats): everyone in it but
    // the phone's owner. Empty for one-to-one conversations, which only have remoteUid.
    // A received message's remoteUid is its sender, a sent one's the first recipient.
    QStringList participants;
    // MMS: the To, Cc and Bcc headers, the parts, and the ids the Android phone gave it
    // (m_id, tr_id, kept so a backup made on Sailfish OS restores into the same message).
    QStringList toList;
    QStringList ccList;
    QStringList bccList;
    QList<HistoryPart> parts;
    QString mmsMessageId;
    QString mmsTransactionId;

    bool isMessage() const { return type == SmsEvent || type == MmsEvent; }
    bool isMms() const { return type == MmsEvent; }
    bool isCall() const { return type == CallEvent; }
    qint64 callDuration() const { return endTime > startTime ? endTime - startTime : 0; }
};

#endif // HISTORYEVENT_H
