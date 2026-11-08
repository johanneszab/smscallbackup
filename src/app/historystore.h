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

    bool open(QString *error);

    struct Counts
    {
        int sms = 0;
        int mms = 0;
        int calls = 0;
    };
    bool counts(Counts *counts, QString *error);

    // Streams the events of the given types, oldest first. Stops when the callback
    // returns false.
    bool readEvents(const QList<int> &types, const std::function<bool(const HistoryEvent &)> &callback,
                    QString *error);

    // Mapping::duplicateKey() of every SMS, MMS and call on the phone.
    bool duplicateKeys(QSet<QByteArray> *keys, QString *error);

    // Conversations of an account with a single recipient: libcommhistory's match key of
    // the recipient (see HistoryWriter::groupKey()) -> group id.
    bool groupsOf(const QString &localUid, const std::function<QString(const QString &)> &keyOf,
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
