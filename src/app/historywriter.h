// SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
// SPDX-License-Identifier: MIT

#ifndef HISTORYWRITER_H
#define HISTORYWRITER_H

#include "historyevent.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QScopedPointer>
#include <QString>
#include <QStringList>

namespace CommHistory {
class EventModel;
class GroupManager;
}

// Adds events to the communication history through libcommhistory, the way commhistoryd
// and the Phone app store them: messages go into the existing conversation with the
// same number (or a new one), group messages into a conversation with all its members,
// calls into the call log. libcommhistory announces every change on D-Bus, so the
// Messages and Phone apps and commhistoryd pick them up at once.
//
// MMS attachments must already be in files (HistoryPart::path, see RestoreTask). Like
// commhistoryd does it, an MMS is added first and its files are then moved into
// commhistory/data/<event id>/ (commhistoryd removes folders there that belong to no
// event), and the event updated with them.
//
// Runs in the thread that owns libcommhistory's database connection (the main thread),
// a batch of events per event-loop turn so the UI stays responsive. Each batch is one
// transaction; cancelling keeps what was added so far, and restoring the same backup
// again only adds what is missing.
class HistoryWriter : public QObject
{
    Q_OBJECT

public:
    explicit HistoryWriter(QObject *parent = nullptr);
    ~HistoryWriter() override;

    // events: sorted by time. localUid: the SIM account to attach them to.
    void start(const QList<HistoryEvent> &events, const QString &localUid,
               const QString &subscriberIdentity);
    void cancel();
    bool isRunning() const { return m_running; }

    // libcommhistory's comparison key for a phone number on a cellular account.
    static QString groupKey(const QString &remoteUid);
    // Key of a conversation with these members (in any order).
    static QString conversationKey(const QStringList &remoteUids);
    static QString conversationKey(const HistoryEvent &event);

signals:
    void progress(int done, int total);
    void finished(bool ok, int messages, int calls, int conversations, const QString &error);

private slots:
    void step();
    void finish(bool ok, const QString &error);

private:
    bool prepareConversations(QString *error);

    QList<HistoryEvent> m_events;
    QString m_localUid;
    QString m_subscriberIdentity;
    QString m_tokenPrefix;
    QHash<QString, int> m_groups;       // conversationKey -> group id
    QScopedPointer<CommHistory::EventModel> m_model;
    QScopedPointer<CommHistory::GroupManager> m_groupManager;
    int m_next = 0;
    int m_messages = 0;
    int m_calls = 0;
    int m_conversations = 0;
    bool m_running = false;
    bool m_cancel = false;
};

#endif // HISTORYWRITER_H
