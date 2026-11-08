// SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
// SPDX-License-Identifier: MIT

#include "backuptask.h"

#include "contactnames.h"
#include "historystore.h"
#include "mapping.h"
#include "sbrwriter.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>
#include <functional>

namespace {

QString creator()
{
    return QStringLiteral("SMS & Call Backup %1 for Sailfish OS").arg(QCoreApplication::applicationVersion());
}

QString uniquePath(const QString &folder, const QString &prefix, const QString &stamp)
{
    const QDir dir(folder);
    QString path = dir.filePath(prefix + stamp + QStringLiteral(".xml"));
    for (int n = 1; QFileInfo::exists(path); ++n)
        path = dir.filePath(QStringLiteral("%1%2-%3.xml").arg(prefix, stamp).arg(n));
    return path;
}

struct FileJob
{
    QString prefix;
    SbrWriter::Kind kind;
    const QList<HistoryEvent> *events;
};

BackupTask::WriteResult writeFiles(const QString &folder, const QList<FileJob> &jobs,
                                   const QHash<QString, QString> &names, std::atomic<bool> *cancel,
                                   const std::function<void(qreal)> &progress)
{
    BackupTask::WriteResult result;
    if (!QDir().mkpath(folder)) {
        result.error = QCoreApplication::translate("BackupTask", "Cannot create the folder %1.").arg(folder);
        return result;
    }
    // One backup run: both files share the time stamp and the backup_set id, like the
    // files of the Android app.
    const QDateTime now = QDateTime::currentDateTime();
    const QString stamp = now.toString(QStringLiteral("yyyyMMddHHmmss"));
    const qint64 backupDate = now.toMSecsSinceEpoch();
    const QString backupSet = QUuid::createUuid().toString().mid(1, 36);

    int total = 0;
    for (const FileJob &job : jobs)
        total += job.events->size();
    int done = 0;

    for (const FileJob &job : jobs) {
        const QString path = uniquePath(folder, job.prefix, stamp);
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            result.error = QCoreApplication::translate("BackupTask", "Cannot write %1: %2").arg(path, file.errorString());
            return result;
        }
        SbrWriter writer(&file, job.kind);
        bool ok = writer.begin(job.events->size(), backupSet, backupDate, creator());
        for (const HistoryEvent &e : *job.events) {
            if (!ok)
                break;
            const QString name = names.value(e.remoteUid);
            ok = job.kind == SbrWriter::MessagesKind ? writer.writeSms(Mapping::eventToSms(e, name))
                                                     : writer.writeCall(Mapping::eventToCall(e, name));
            if (++done % 250 == 0) {
                progress(0.2 + 0.8 * qreal(done) / qMax(total, 1));
                if (cancel->load()) {
                    file.cancelWriting();
                    result.cancelled = true;
                    return result;
                }
            }
        }
        if (ok)
            ok = writer.end();
        if (!ok) {
            file.cancelWriting();
            result.error = QCoreApplication::translate("BackupTask", "Writing %1 failed: %2").arg(path, writer.errorString());
            return result;
        }
        if (!file.commit()) {
            result.error = QCoreApplication::translate("BackupTask", "Writing %1 failed: %2").arg(path, file.errorString());
            return result;
        }
        result.files.append(path);
    }
    result.ok = true;
    return result;
}

} // namespace

BackupTask::BackupTask(QObject *parent)
    : QObject(parent)
    , m_cancel(false)
{
    connect(&m_readWatcher, SIGNAL(finished()), this, SLOT(readDone()));
    connect(&m_writeWatcher, SIGNAL(finished()), this, SLOT(writeDone()));
}

BackupTask::~BackupTask()
{
    m_cancel = true;
    m_readWatcher.waitForFinished();
    m_writeWatcher.waitForFinished();
}

void BackupTask::start(const QString &folder, bool messages, bool calls, bool contactNames)
{
    if (m_running)
        return;
    if (!messages && !calls) {
        emit finished(false, tr("Choose what to back up."), QStringList(), 0, 0);
        return;
    }
    m_folder = folder;
    m_messages = messages;
    m_calls = calls;
    m_contactNames = contactNames && ContactNames::available();
    m_cancel = false;
    m_running = true;
    emit runningChanged();
    setProgress(0, tr("Reading the history"));

    QList<int> types;
    if (messages)
        types << HistoryEvent::SmsEvent;
    if (calls)
        types << HistoryEvent::CallEvent;
    std::atomic<bool> *cancel = &m_cancel;
    m_readWatcher.setFuture(QtConcurrent::run([types, cancel]() {
        ReadResult result;
        HistoryStore store;
        if (!store.open(&result.error))
            return result;
        result.ok = store.readEvents(types, [&result, cancel](const HistoryEvent &e) {
            if (e.isCall())
                result.calls.append(e);
            else
                result.messages.append(e);
            return !cancel->load();
        }, &result.error);
        return result;
    }));
}

void BackupTask::cancel()
{
    m_cancel = true;
}

bool BackupTask::contactNamesAvailable() const
{
    return ContactNames::available();
}

QVariantMap BackupTask::historyCounts() const
{
    QVariantMap result;
    HistoryStore store;
    QString error;
    HistoryStore::Counts counts;
    const bool ok = store.open(&error) && store.counts(&counts, &error);
    result.insert(QStringLiteral("ok"), ok);
    result.insert(QStringLiteral("error"), error);
    result.insert(QStringLiteral("sms"), counts.sms);
    result.insert(QStringLiteral("mms"), counts.mms);
    result.insert(QStringLiteral("calls"), counts.calls);
    return result;
}

void BackupTask::readDone()
{
    const ReadResult result = m_readWatcher.result();
    if (m_cancel) {
        finish(false, tr("Backup cancelled."));
        return;
    }
    if (!result.ok) {
        finish(false, result.error);
        return;
    }
    m_messageEvents = result.messages;
    m_callEvents = result.calls;
    if (!m_contactNames) {
        namesDone();
        return;
    }
    setProgress(0.1, tr("Looking up contact names"));
    QSet<QString> numbers;
    for (const HistoryEvent &e : m_messageEvents)
        numbers.insert(e.remoteUid);
    for (const HistoryEvent &e : m_callEvents)
        numbers.insert(e.remoteUid);
    QStringList list;
    for (const QString &number : numbers)
        list.append(number);
    if (!m_names) {
        m_names = new ContactNames(this);
        connect(m_names, SIGNAL(finished()), this, SLOT(namesDone()));
    }
    m_names->resolve(list);
}

void BackupTask::namesDone()
{
    if (!m_running)
        return;
    if (m_cancel) {
        finish(false, tr("Backup cancelled."));
        return;
    }
    const QHash<QString, QString> names = (m_contactNames && m_names) ? m_names->names()
                                                                      : QHash<QString, QString>();
    setProgress(0.2, tr("Writing the backup"));

    QList<FileJob> jobs;
    if (m_messages)
        jobs.append(FileJob { QStringLiteral("sms-"), SbrWriter::MessagesKind, &m_messageEvents });
    if (m_calls)
        jobs.append(FileJob { QStringLiteral("calls-"), SbrWriter::CallsKind, &m_callEvents });
    const QString folder = m_folder;
    std::atomic<bool> *cancel = &m_cancel;
    BackupTask *self = this;
    m_writeWatcher.setFuture(QtConcurrent::run([folder, jobs, names, cancel, self]() {
        return writeFiles(folder, jobs, names, cancel, [self](qreal fraction) {
            QMetaObject::invokeMethod(self, "setProgress", Qt::QueuedConnection, Q_ARG(qreal, fraction),
                                      Q_ARG(QString, QCoreApplication::translate("BackupTask", "Writing the backup")));
        });
    }));
}

void BackupTask::writeDone()
{
    const WriteResult result = m_writeWatcher.result();
    if (result.cancelled || m_cancel) {
        finish(false, tr("Backup cancelled."));
        return;
    }
    if (!result.ok) {
        finish(false, result.error);
        return;
    }
    finish(true, QString(), result.files);
}

void BackupTask::setProgress(qreal progress, const QString &status)
{
    if (!m_running && progress > 0)
        return;
    m_progress = progress;
    m_status = status;
    emit progressChanged();
}

void BackupTask::finish(bool ok, const QString &message, const QStringList &files)
{
    const int messages = m_messages ? m_messageEvents.size() : 0;
    const int calls = m_calls ? m_callEvents.size() : 0;
    m_messageEvents.clear();
    m_callEvents.clear();
    m_running = false;
    m_progress = 0;
    m_status.clear();
    emit progressChanged();
    emit runningChanged();
    emit finished(ok, message, files, ok ? messages : 0, ok ? calls : 0);
}
