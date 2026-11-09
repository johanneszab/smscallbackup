// SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
// SPDX-License-Identifier: MIT

#include "backuptask.h"

#include "contactnames.h"
#include "historystore.h"
#include "mapping.h"
#include "sbrreader.h"
#include "sbrwriter.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>
#include <fcntl.h>
#include <functional>
#include <sys/file.h>
#include <unistd.h>

namespace {

typedef std::function<void(qreal)> Progress;

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

// Loads the files of an MMS's parts. forKey: only what tells entries apart (the texts,
// and which attachments are there), for comparing with an archive file.
void loadParts(HistoryEvent *e, bool forKey, int *missing)
{
    QList<HistoryPart> loaded;
    for (HistoryPart part : e->parts) {
        if (forKey && part.isSmil())
            continue;
        if (forKey && part.isMedia()) {
            if (!QFileInfo(part.path).isFile())
                continue;
            part.data = QByteArrayLiteral("-");     // only whether there is content matters
            loaded.append(part);
            continue;
        }
        QFile file(part.path);
        if (!file.open(QIODevice::ReadOnly)) {
            if (missing && part.isMedia())
                ++*missing;
            continue;
        }
        part.data = file.readAll();
        loaded.append(part);
    }
    e->parts = loaded;
}

QByteArray phoneKey(const HistoryEvent &event)
{
    if (event.isCall())
        return Mapping::archiveKey(Mapping::eventToCall(event, QString()));
    if (!event.isMms())
        return Mapping::archiveKey(Mapping::eventToSms(event, QString()));
    HistoryEvent e = event;
    loadParts(&e, true, nullptr);
    return Mapping::archiveKey(Mapping::eventToMms(e, QString()));
}

bool writeEvent(SbrWriter *writer, const HistoryEvent &event, const QString &name, int *missing)
{
    if (event.isCall())
        return writer->writeCall(Mapping::eventToCall(event, name));
    if (!event.isMms())
        return writer->writeSms(Mapping::eventToSms(event, name));
    HistoryEvent e = event;
    loadParts(&e, false, missing);
    return writer->writeMms(Mapping::eventToMms(e, name));
}

struct FileJob
{
    bool messages;
    const QList<HistoryEvent> *events;
};

struct Run
{
    QString backupSet;
    qint64 backupDate = 0;
    QString stamp;
    const QHash<QString, QString> *names = nullptr;
    std::atomic<bool> *cancel = nullptr;
    int missingParts = 0;
};

// A new file with all entries.
bool writeFull(const QString &folder, const FileJob &job, Run *run, const Progress &progress,
               BackupTask::FileResult *result, QString *error, bool *cancelled)
{
    const QString path = uniquePath(folder, job.messages ? QStringLiteral("sms-") : QStringLiteral("calls-"), run->stamp);
    result->path = path;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        *error = QCoreApplication::translate("BackupTask", "Cannot write %1: %2").arg(path, file.errorString());
        return false;
    }
    SbrWriter writer(&file, job.messages ? SbrWriter::MessagesKind : SbrWriter::CallsKind);
    bool ok = writer.begin(job.events->size(), run->backupSet, run->backupDate, creator());
    int done = 0;
    for (const HistoryEvent &e : *job.events) {
        if (!ok)
            break;
        ok = writeEvent(&writer, e, run->names->value(e.remoteUid), &run->missingParts);
        if (e.isMms())
            ++result->writtenMms;
        if (++done % 100 == 0) {
            progress(qreal(done) / qMax(1, job.events->size()));
            if (run->cancel->load()) {
                file.cancelWriting();
                *cancelled = true;
                return false;
            }
        }
    }
    if (ok)
        ok = writer.end();
    if (!ok) {
        file.cancelWriting();
        *error = QCoreApplication::translate("BackupTask", "Writing %1 failed: %2").arg(path, writer.errorString());
        return false;
    }
    if (!file.commit()) {
        *error = QCoreApplication::translate("BackupTask", "Writing %1 failed: %2").arg(path, file.errorString());
        return false;
    }
    result->written = job.events->size();
    result->total = job.events->size();
    return true;
}

// sms.xml / calls.xml: what is in the file stays as it is, the phone's entries that are
// not in it yet are added at the end. The file is only replaced once the new one is
// complete, and not at all if nothing is new.
bool writeArchive(const QString &folder, const FileJob &job, Run *run, const Progress &progress,
                  BackupTask::FileResult *result, QString *error, bool *cancelled)
{
    const QString path = QDir(folder).filePath(BackupTask::archiveFileName(job.messages));
    const QString name = QFileInfo(path).fileName();
    result->path = path;
    const SbrReader::Kind kind = job.messages ? SbrReader::MessagesKind : SbrReader::CallsKind;
    const bool exists = QFileInfo::exists(path);

    // 1. What the file has, as a count per entry (the same message can be there twice).
    QHash<QByteArray, int> inFile;
    int fileCount = 0;
    if (exists) {
        SbrReader reader(path);
        SbrReader::Callbacks callbacks;
        callbacks.sms = [&](const Sbr::SmsRecord &r) { ++inFile[Mapping::archiveKey(r)]; ++fileCount; return !run->cancel->load(); };
        callbacks.mms = [&](const Sbr::MmsRecord &r) { ++inFile[Mapping::archiveKey(r)]; ++fileCount; return !run->cancel->load(); };
        callbacks.call = [&](const Sbr::CallRecord &r) { ++inFile[Mapping::archiveKey(r)]; ++fileCount; return !run->cancel->load(); };
        callbacks.progress = [&](qint64 done, qint64 total) {
            progress(0.3 * done / qMax<qint64>(1, total));
            return !run->cancel->load();
        };
        if (!reader.read(callbacks)) {
            if (reader.wasCancelled()) {
                *cancelled = true;
            } else {
                *error = QCoreApplication::translate("BackupTask", "%1 cannot be read, so it was left as it is: %2 Move it away to start a new one.")
                        .arg(name, reader.errorString());
            }
            return false;
        }
        if (reader.info().kind != kind) {
            *error = QCoreApplication::translate("BackupTask", "%1 is not a backup of %2, so it was left as it is. Move it away to start a new one.")
                    .arg(name, job.messages ? QCoreApplication::translate("BackupTask", "messages") : QCoreApplication::translate("BackupTask", "calls"));
            return false;
        }
    }

    // 2. The phone's entries that are not in the file yet.
    QList<int> fresh;
    for (int i = 0; i < job.events->size(); ++i) {
        const QByteArray key = phoneKey(job.events->at(i));
        QHash<QByteArray, int>::iterator it = inFile.find(key);
        if (it != inFile.end() && it.value() > 0)
            --it.value();
        else
            fresh.append(i);
        if (i % 500 == 0 && run->cancel->load()) {
            *cancelled = true;
            return false;
        }
    }
    result->total = fileCount + fresh.size();
    if (exists && fresh.isEmpty()) {
        result->changed = false;
        progress(1);
        return true;
    }

    // 3. The old entries as they are, then the new ones.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        *error = QCoreApplication::translate("BackupTask", "Cannot write %1: %2").arg(path, file.errorString());
        return false;
    }
    SbrWriter writer(&file, job.messages ? SbrWriter::MessagesKind : SbrWriter::CallsKind);
    bool ok = writer.begin(fileCount + fresh.size(), run->backupSet, run->backupDate, creator(),
                           QStringLiteral("archive"));
    if (ok && exists) {
        int copied = 0;
        SbrReader reader(path);
        SbrReader::Callbacks callbacks;
        callbacks.sms = [&](const Sbr::SmsRecord &r) {
            ++copied;
            return writer.writeElement("sms", r.attributes) && !run->cancel->load();
        };
        callbacks.mms = [&](const Sbr::MmsRecord &r) {
            ++copied;
            return writer.writeMms(r) && !run->cancel->load();
        };
        callbacks.call = [&](const Sbr::CallRecord &r) {
            ++copied;
            return writer.writeElement("call", r.attributes) && !run->cancel->load();
        };
        callbacks.progress = [&](qint64 done, qint64 total) {
            progress(0.3 + 0.5 * done / qMax<qint64>(1, total));
            return !run->cancel->load();
        };
        ok = reader.read(callbacks);
        if (!ok) {
            file.cancelWriting();
            if (run->cancel->load()) {
                *cancelled = true;
            } else if (!writer.errorString().isEmpty()) {
                *error = QCoreApplication::translate("BackupTask", "Writing %1 failed: %2").arg(path, writer.errorString());
            } else {
                *error = QCoreApplication::translate("BackupTask", "%1 cannot be read, so it was left as it is: %2").arg(name, reader.errorString());
            }
            return false;
        }
        if (copied != fileCount) {
            file.cancelWriting();
            *error = QCoreApplication::translate("BackupTask", "%1 was changed during the backup, so it was left as it is. Please try again.").arg(name);
            return false;
        }
    }
    int done = 0;
    for (int index : fresh) {
        if (!ok)
            break;
        const HistoryEvent &e = job.events->at(index);
        ok = writeEvent(&writer, e, run->names->value(e.remoteUid), &run->missingParts);
        if (e.isMms())
            ++result->writtenMms;
        if (++done % 100 == 0) {
            progress(0.8 + 0.2 * done / qMax(1, fresh.size()));
            if (run->cancel->load()) {
                file.cancelWriting();
                *cancelled = true;
                return false;
            }
        }
    }
    if (ok)
        ok = writer.end();
    if (!ok) {
        file.cancelWriting();
        *error = QCoreApplication::translate("BackupTask", "Writing %1 failed: %2").arg(path, writer.errorString());
        return false;
    }
    if (!file.commit()) {
        *error = QCoreApplication::translate("BackupTask", "Writing %1 failed: %2").arg(path, file.errorString());
        return false;
    }
    result->written = fresh.size();
    return true;
}

BackupTask::WriteResult writeFiles(const QString &folder, const QList<FileJob> &jobs, bool incremental,
                                   const QHash<QString, QString> &names, std::atomic<bool> *cancel,
                                   const Progress &progress)
{
    BackupTask::WriteResult result;
    if (!QDir().mkpath(folder)) {
        result.error = QCoreApplication::translate("BackupTask", "Cannot create the folder %1.").arg(folder);
        return result;
    }
    // One backup run: both files share the time and the backup_set id, like the files
    // of the Android app.
    const QDateTime now = QDateTime::currentDateTime();
    Run run;
    run.stamp = now.toString(QStringLiteral("yyyyMMddHHmmss"));
    run.backupDate = now.toMSecsSinceEpoch();
    run.backupSet = QUuid::createUuid().toString().mid(1, 36);
    run.names = &names;
    run.cancel = cancel;

    int total = 0;
    for (const FileJob &job : jobs)
        total += job.events->size() + 1;
    int before = 0;
    for (const FileJob &job : jobs) {
        const int share = job.events->size() + 1;
        const Progress jobProgress = [&progress, before, share, total](qreal fraction) {
            progress(0.2 + 0.8 * (before + fraction * share) / qMax(1, total));
        };
        BackupTask::FileResult file;
        file.messages = job.messages;
        const bool ok = incremental
                ? writeArchive(folder, job, &run, jobProgress, &file, &result.error, &result.cancelled)
                : writeFull(folder, job, &run, jobProgress, &file, &result.error, &result.cancelled);
        if (!ok)
            return result;
        result.files.append(file);
        before += share;
    }
    result.missingParts = run.missingParts;
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
    unlock();
}

QString BackupTask::archiveFileName(bool messages)
{
    return messages ? QStringLiteral("sms.xml") : QStringLiteral("calls.xml");
}

bool BackupTask::lock()
{
    if (m_lockFd >= 0)
        return true;
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(dir);
    const QByteArray path = QFile::encodeName(dir + QStringLiteral("/backup.lock"));
    const int fd = ::open(path.constData(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0)
        return true;    // cannot lock: better back up than not
    // flock() works across Sailjail's mount namespaces, unlike PID-based lock files.
    if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
        ::close(fd);
        return false;
    }
    m_lockFd = fd;
    return true;
}

void BackupTask::unlock()
{
    if (m_lockFd >= 0) {
        ::flock(m_lockFd, LOCK_UN);
        ::close(m_lockFd);
        m_lockFd = -1;
    }
}

void BackupTask::start(const QString &folder, bool messages, bool calls, bool contactNames, bool incremental)
{
    if (m_running)
        return;
    if (!messages && !calls) {
        emit finished(false, tr("Choose what to back up."), QVariantMap());
        return;
    }
    if (!lock()) {
        QVariantMap result;
        result.insert(QStringLiteral("busy"), true);
        emit finished(false, tr("Another backup is running at the moment. Please try again in a minute."), result);
        return;
    }
    m_folder = folder;
    m_messages = messages;
    m_calls = calls;
    m_contactNames = contactNames && ContactNames::available();
    m_incremental = incremental;
    m_cancel = false;
    m_running = true;
    emit runningChanged();
    setProgress(0, tr("Reading the history"));

    QList<int> types;
    if (messages)
        types << HistoryEvent::SmsEvent << HistoryEvent::MmsEvent;
    if (calls)
        types << HistoryEvent::CallEvent;
    std::atomic<bool> *cancel = &m_cancel;
    m_readWatcher.setFuture(QtConcurrent::run([types, cancel]() {
        ReadResult result;
        if (!HistoryStore::exists()) {
            result.ok = true;   // a phone without any history yet
            return result;
        }
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
    HistoryStore::Counts counts;
    QString error;
    bool ok = true;
    if (HistoryStore::exists()) {
        HistoryStore store;
        ok = store.open(&error) && store.counts(&counts, &error);
    }
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
        jobs.append(FileJob { true, &m_messageEvents });
    if (m_calls)
        jobs.append(FileJob { false, &m_callEvents });
    const QString folder = m_folder;
    const bool incremental = m_incremental;
    std::atomic<bool> *cancel = &m_cancel;
    BackupTask *self = this;
    m_writeWatcher.setFuture(QtConcurrent::run([folder, jobs, incremental, names, cancel, self]() {
        return writeFiles(folder, jobs, incremental, names, cancel, [self](qreal fraction) {
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
    finish(true, QString(), result);
}

void BackupTask::setProgress(qreal progress, const QString &status)
{
    if (!m_running && progress > 0)
        return;
    m_progress = progress;
    m_status = status;
    emit progressChanged();
}

void BackupTask::finish(bool ok, const QString &message)
{
    finish(ok, message, WriteResult());
}

void BackupTask::finish(bool ok, const QString &message, const WriteResult &write)
{
    QVariantMap result;
    QStringList files;
    int messages = 0;
    int mms = 0;
    int calls = 0;
    int messagesTotal = 0;
    int callsTotal = 0;
    for (const FileResult &file : write.files) {
        if (file.changed)
            files.append(file.path);
        if (file.messages) {
            messages += file.written;
            mms += file.writtenMms;
            messagesTotal += file.total;
        } else {
            calls += file.written;
            callsTotal += file.total;
        }
    }
    result.insert(QStringLiteral("files"), files);
    result.insert(QStringLiteral("messages"), messages);
    result.insert(QStringLiteral("mms"), mms);
    result.insert(QStringLiteral("calls"), calls);
    result.insert(QStringLiteral("messagesTotal"), messagesTotal);
    result.insert(QStringLiteral("callsTotal"), callsTotal);
    result.insert(QStringLiteral("incremental"), m_incremental);
    result.insert(QStringLiteral("missingParts"), write.missingParts);

    m_messageEvents.clear();
    m_callEvents.clear();
    unlock();
    m_running = false;
    m_progress = 0;
    m_status.clear();
    emit progressChanged();
    emit runningChanged();
    emit finished(ok, message, result);
}
