// SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
// SPDX-License-Identifier: MIT

#ifndef RESTORETASK_H
#define RESTORETASK_H

#include "historyevent.h"

#include <QFutureWatcher>
#include <QList>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <atomic>

class HistoryWriter;

// Restores SMS Backup & Restore files into the communication history in two steps:
// analyze() reads the files and finds what is not on the phone yet (in a worker thread),
// restore() adds that through libcommhistory (HistoryWriter).
class RestoreTask : public QObject
{
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString status READ status NOTIFY progressChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    // files, messages, newMessages, calls, newCalls, mms, skipped (list of texts),
    // firstTime, lastTime (ms, of the new entries)
    Q_PROPERTY(QVariantMap summary READ summary NOTIFY stateChanged)
    // [{ localUid, name }], the account used most recently first
    Q_PROPERTY(QVariantList accounts READ accounts NOTIFY stateChanged)
    // after restoring: { messages, calls, conversations }
    Q_PROPERTY(QVariantMap result READ result NOTIFY stateChanged)

public:
    enum State {
        Idle,
        Analyzing,
        Ready,          // analyzed, summary and accounts are set
        Restoring,
        Finished,
        Failed
    };
    Q_ENUM(State)

    explicit RestoreTask(QObject *parent = nullptr);
    ~RestoreTask() override;

    Q_INVOKABLE void analyze(const QStringList &files);
    Q_INVOKABLE void restore(const QString &localUid, bool markAsRead);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void reset();

    State state() const { return m_state; }
    qreal progress() const { return m_progress; }
    QString status() const { return m_status; }
    QString error() const { return m_error; }
    QVariantMap summary() const { return m_summary; }
    QVariantList accounts() const { return m_accounts; }
    QVariantMap result() const { return m_result; }

    struct Analysis
    {
        bool ok = false;
        bool cancelled = false;
        QString error;
        QList<HistoryEvent> events;     // not on the phone yet, sorted by time
        QVariantMap summary;
        QStringList accountsWithHistory;
    };

signals:
    void stateChanged();
    void progressChanged();

private slots:
    void analysisDone();
    void writerProgress(int done, int total);
    void writerFinished(bool ok, int messages, int calls, int conversations, const QString &error);
    void setProgress(qreal progress, const QString &status);

private:
    void setState(State state, const QString &error = QString());

    State m_state = Idle;
    qreal m_progress = 0;
    QString m_status;
    QString m_error;
    QVariantMap m_summary;
    QVariantList m_accounts;
    QVariantMap m_result;
    QList<HistoryEvent> m_events;
    std::atomic<bool> m_cancel;
    QFutureWatcher<Analysis> m_watcher;
    HistoryWriter *m_writer = nullptr;
};

#endif // RESTORETASK_H
