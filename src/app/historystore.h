// SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
// SPDX-License-Identifier: MIT

#ifndef HISTORYSTORE_H
#define HISTORYSTORE_H

#include "historyevent.h"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <functional>

// Read-only access to the communication history database (libcommhistory schema 4,
// ~/.local/share/commhistory/commhistory.db). Reading SQLite directly is the fastest way
// to stream the complete history; all writes go through libcommhistory (HistoryWriter),
// so the Messages and Phone apps are notified.
//
// Each instance has its own connection, so it can be used from a worker thread; create
// and destroy it in the thread that uses it.
class HistoryStore
{
public:
    explicit HistoryStore(const QString &path = QString());
    ~HistoryStore();

    static QString defaultPath();
    // False on a phone that never had any calls or messages: libcommhistory creates the
    // database on first use.
    static bool exists();
    // ~/.local/share/commhistory: the database, and the MMS parts in data/<event id>/.
    static QString commhistoryDir();
    // Where a restore keeps the attachments of MMS until their events exist. Inside the
    // commhistory folder, so they can be moved into data/<event id>/ (Sailjail allows
    // nothing else there, and moving between its mounts fails).
    static QString stagingRoot();

    // Event properties this app adds to restored MMS: the m_id and tr_id of the backup.
    static const char *const MmsMessageIdProperty;
    static const char *const MmsTransactionIdProperty;

    bool open(QString *error);

    struct Counts
    {
        int sms = 0;
        int mms = 0;
        int calls = 0;
    };
    bool counts(Counts *counts, QString *error);

    // Streams the events of the given types, oldest first. Stops when the callback
    // returns false. MMS come with their parts (paths, not the data), the To/Cc/Bcc
    // headers, and the members of group conversations in participants.
    bool readEvents(const QList<int> &types, const std::function<bool(const HistoryEvent &)> &callback,
                    QString *error);

    // Mapping::duplicateKey() of every SMS, MMS and call on the phone.
    bool duplicateKeys(QSet<QByteArray> *keys, QString *error);

    // Conversations of an account: key (see HistoryWriter::conversationKey()) -> group id.
    bool conversations(const QString &localUid, const std::function<QString(const QStringList &)> &keyOf,
                       QHash<QString, int> *groups, QString *error);

    // Cellular accounts with history on this phone, most recently used first.
    QStringList cellularAccounts();
    // The SIM's IMSI as stored with the newest event of the account, empty if unknown.
    QString subscriberIdentity(const QString &localUid);

private:
    QString m_path;
    QString m_connection;
};

#endif // HISTORYSTORE_H
