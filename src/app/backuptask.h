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

// Writes the phone's messages (SMS and MMS) and call log in the format of SMS Backup &
// Restore: either into new files sms-<time>.xml and calls-<time>.xml, or incrementally
// into sms.xml and calls.xml, which keep everything and get what is new added (the
// "archive" files of the Android app). Reading the database and writing the files happen
// in worker threads; only the contact lookup runs in the main thread.
//
// Only one backup runs at a time, also across processes (the app and the automatic
// backup), so an archive file is never written twice at once.
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

    Q_INVOKABLE void start(const QString &folder, bool messages, bool calls, bool contactNames,
                           bool incremental);
    Q_INVOKABLE void cancel();
    // { ok, error, sms, mms, calls }: what the history on the phone holds
    Q_INVOKABLE QVariantMap historyCounts() const;

    bool isRunning() const { return m_running; }
    bool contactNamesAvailable() const;
    qreal progress() const { return m_progress; }
    QString status() const { return m_status; }

    // The files of an incremental backup.
    static QString archiveFileName(bool messages);

    struct ReadResult
    {
        bool ok = false;
        QString error;
        QList<HistoryEvent> messages;
        QList<HistoryEvent> calls;
    };
    struct FileResult
    {
        QString path;
        bool messages = true;
        int written = 0;        // entries written: all, or the new ones of an archive
        int writtenMms = 0;
        int total = 0;          // entries in the file
        bool changed = true;    // false: an archive file without anything new, left as it was
    };
    struct WriteResult
    {
        bool ok = false;
        bool cancelled = false;
        QString error;
        QList<FileResult> files;
        int missingParts = 0;   // MMS attachments whose file was gone
    };

signals:
    void runningChanged();
    void progressChanged();
    // result: files (written or updated), messages, mms, calls (entries written: all of
    // them, or what an incremental backup added), messagesTotal, callsTotal (entries in the
    // files), incremental, missingParts (MMS attachments whose file was gone), busy
    // (another backup was running, nothing was done).
    void finished(bool ok, const QString &message, const QVariantMap &result);

private slots:
    void readDone();
    void namesDone();
    void writeDone();
    void setProgress(qreal progress, const QString &status);

private:
    void finish(bool ok, const QString &message);
    void finish(bool ok, const QString &message, const WriteResult &result);
    bool lock();
    void unlock();

    QString m_folder;
    bool m_messages = true;
    bool m_calls = true;
    bool m_contactNames = true;
    bool m_incremental = true;
    bool m_running = false;
    qreal m_progress = 0;
    QString m_status;
    std::atomic<bool> m_cancel;
    int m_lockFd = -1;
    QList<HistoryEvent> m_messageEvents;
    QList<HistoryEvent> m_callEvents;
    ContactNames *m_names = nullptr;
    QFutureWatcher<ReadResult> m_readWatcher;
    QFutureWatcher<WriteResult> m_writeWatcher;
};

#endif // BACKUPTASK_H
