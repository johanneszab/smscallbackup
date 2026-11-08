// SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
// SPDX-License-Identifier: MIT

#ifndef BACKUPTASK_H
#define BACKUPTASK_H

#include "historyevent.h"

#include <QFutureWatcher>
#include <QHash>
#include <QList>
#include <QObject>
#include <QStringList>
#include <QVariantMap>
#include <atomic>

class ContactNames;

// Writes the phone's messages and call log to sms-<time>.xml and calls-<time>.xml in the
// format of SMS Backup & Restore. Reading the database and writing the files happen in
// worker threads; only the contact lookup runs in the main thread.
class BackupTask : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString status READ status NOTIFY progressChanged)
    // Whether contact names can be added (needs read access to the contacts database)
    Q_PROPERTY(bool contactNamesAvailable READ contactNamesAvailable CONSTANT)

public:
    explicit BackupTask(QObject *parent = nullptr);
    ~BackupTask() override;

    Q_INVOKABLE void start(const QString &folder, bool messages, bool calls, bool contactNames);
    Q_INVOKABLE void cancel();
    // { ok, error, sms, mms, calls }: what the history on the phone holds
    Q_INVOKABLE QVariantMap historyCounts() const;

    bool isRunning() const { return m_running; }
    bool contactNamesAvailable() const;
    qreal progress() const { return m_progress; }
    QString status() const { return m_status; }

    struct ReadResult
    {
        bool ok = false;
        QString error;
        QList<HistoryEvent> messages;
        QList<HistoryEvent> calls;
    };
    struct WriteResult
    {
        bool ok = false;
        bool cancelled = false;
        QString error;
        QStringList files;
    };

signals:
    void runningChanged();
    void progressChanged();
    void finished(bool ok, const QString &message, const QStringList &files, int messages, int calls);

private slots:
    void readDone();
    void namesDone();
    void writeDone();
    void setProgress(qreal progress, const QString &status);

private:
    void finish(bool ok, const QString &message, const QStringList &files = QStringList());

    QString m_folder;
    bool m_messages = true;
    bool m_calls = true;
    bool m_contactNames = true;
    bool m_running = false;
    qreal m_progress = 0;
    QString m_status;
    std::atomic<bool> m_cancel;
    QList<HistoryEvent> m_messageEvents;
    QList<HistoryEvent> m_callEvents;
    ContactNames *m_names = nullptr;
    QFutureWatcher<ReadResult> m_readWatcher;
    QFutureWatcher<WriteResult> m_writeWatcher;
};

#endif // BACKUPTASK_H
