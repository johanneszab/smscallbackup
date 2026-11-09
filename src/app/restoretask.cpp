// SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
// SPDX-License-Identifier: MIT

#include "restoretask.h"

#include "historystore.h"
#include "historywriter.h"
#include "mapping.h"
#include "phonenumber.h"
#include "sbrreader.h"
#include "simaccounts.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QSet>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <functional>

namespace {

qint64 sortTime(const HistoryEvent &e)
{
    return e.isCall() ? e.startTime : e.endTime;
}

// Restores that were interrupted (the app was closed or crashed) leave their attachment
// files behind; another restore may be running in the other process (app or command line).
void removeOldStagingDirs()
{
    const QDateTime limit = QDateTime::currentDateTime().addDays(-1);
    const QDir root(HistoryStore::stagingRoot());
    if (!root.exists())
        return;
    for (const QFileInfo &entry : root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (entry.lastModified() < limit)
            QDir(entry.absoluteFilePath()).removeRecursively();
    }
    QDir().rmdir(root.absolutePath());
}

RestoreTask::Analysis analyzeFiles(const QStringList &files, std::atomic<bool> *cancel,
                                   const std::function<void(qreal, const QString &)> &progress)
{
    RestoreTask::Analysis analysis;

    // What is on the phone already: compared while reading, so attachments of messages
    // that are there already are never stored.
    progress(0, QCoreApplication::translate("RestoreTask", "Reading the history on the phone"));
    QSet<QByteArray> known;
    HistoryStore store;
    const bool haveHistory = HistoryStore::exists();
    if (haveHistory) {
        if (!store.open(&analysis.error) || !store.duplicateKeys(&known, &analysis.error))
            return analysis;
    }

    int messages = 0;
    int mms = 0;
    int calls = 0;
    int newMessages = 0;
    int newMms = 0;
    int newCalls = 0;
    int missingAttachments = 0;
    int staged = 0;
    qint64 first = 0;
    qint64 last = 0;
    QMap<int, int> skipped;
    QSet<QString> ownNumbers;
    QStringList names;
    // The read flag is applied when restoring, so the summary does not depend on it.
    Mapping::ImportOptions options;
    options.markAsRead = false;

    // Counts an entry and keeps it if it is new. MMS attachments go into files now.
    const auto take = [&](HistoryEvent &e) {
        const bool message = e.isMessage();
        if (message)
            ++messages;
        else
            ++calls;
        if (e.isMms())
            ++mms;
        const QByteArray key = Mapping::duplicateKey(e);
        if (known.contains(key))
            return true;        // on the phone already, or twice in the selected files
        known.insert(key);
        if (e.isMms() && !e.parts.isEmpty()) {
            if (analysis.stagingDir.isEmpty()) {
                analysis.stagingDir = HistoryStore::stagingRoot() + QLatin1Char('/')
                        + QUuid::createUuid().toString().mid(1, 36);
                if (!QDir().mkpath(analysis.stagingDir)) {
                    analysis.error = QCoreApplication::translate("RestoreTask", "Cannot create the folder %1.").arg(analysis.stagingDir);
                    return false;
                }
            }
            for (HistoryPart &part : e.parts) {
                QFile file(QDir(analysis.stagingDir).filePath(QString::number(++staged)));
                if (!file.open(QIODevice::WriteOnly) || file.write(part.data) != part.data.size()) {
                    analysis.error = QCoreApplication::translate("RestoreTask", "Storing the attachments of the backup failed: %1").arg(file.errorString());
                    return false;
                }
                part.path = file.fileName();
                part.data.clear();
            }
        }
        analysis.events.append(e);
        if (message)
            ++newMessages;
        else
            ++newCalls;
        if (e.isMms())
            ++newMms;
        const qint64 time = sortTime(e);
        if (first == 0 || time < first)
            first = time;
        last = qMax(last, time);
        return true;
    };

    for (int i = 0; i < files.size(); ++i) {
        const QString name = QFileInfo(files.at(i)).fileName();
        names.append(name);
        SbrReader reader(files.at(i));
        SbrReader::Callbacks callbacks;
        bool failed = false;
        callbacks.sms = [&](const Sbr::SmsRecord &record) {
            HistoryEvent e;
            const Mapping::SkipReason reason = Mapping::smsToEvent(record, options, &e);
            if (reason != Mapping::NotSkipped)
                ++skipped[reason];
            else if (!take(e))
                failed = true;
            return !failed && !cancel->load();
        };
        callbacks.call = [&](const Sbr::CallRecord &record) {
            HistoryEvent e;
            const Mapping::SkipReason reason = Mapping::callToEvent(record, options, &e);
            if (reason != Mapping::NotSkipped)
                ++skipped[reason];
            else if (!take(e))
                failed = true;
            return !failed && !cancel->load();
        };
        callbacks.mms = [&](const Sbr::MmsRecord &record) {
            const QString own = Mapping::ownAddressOf(record);
            if (!own.isEmpty())
                ownNumbers.insert(PhoneNumber::matchKey(own));
            HistoryEvent e;
            const Mapping::SkipReason reason = Mapping::mmsToEvent(record, options, &e, &missingAttachments);
            if (reason != Mapping::NotSkipped)
                ++skipped[reason];
            else if (!take(e))
                failed = true;
            return !failed && !cancel->load();
        };
        callbacks.progress = [&](qint64 done, qint64 total) {
            const qreal file = total > 0 ? qreal(done) / qreal(total) : 0;
            progress(0.05 + 0.9 * (i + file) / files.size(), QCoreApplication::translate("RestoreTask", "Reading %1").arg(name));
            return !cancel->load();
        };
        if (!reader.read(callbacks)) {
            // When storing an attachment failed, analysis.error says why.
            if (!failed && reader.wasCancelled())
                analysis.cancelled = true;
            else if (!failed)
                analysis.error = reader.errorString();
            return analysis;
        }
    }

    // Group conversations whose members only the <addr> lists name can include the
    // owner's own number: other messages of the backup tell which it is.
    if (!ownNumbers.isEmpty()) {
        for (HistoryEvent &e : analysis.events) {
            if (e.participants.isEmpty())
                continue;
            QStringList others;
            for (const QString &member : e.participants) {
                if (!ownNumbers.contains(PhoneNumber::matchKey(member)))
                    others.append(member);
            }
            e.participants = others.size() > 1 ? others : QStringList();
        }
    }

    std::stable_sort(analysis.events.begin(), analysis.events.end(),
                     [](const HistoryEvent &a, const HistoryEvent &b) { return sortTime(a) < sortTime(b); });
    if (haveHistory)
        analysis.accountsWithHistory = store.cellularAccounts();

    QVariantList skippedList;
    for (QMap<int, int>::const_iterator it = skipped.constBegin(); it != skipped.constEnd(); ++it) {
        QVariantMap entry;
        entry.insert(QStringLiteral("count"), it.value());
        entry.insert(QStringLiteral("reason"), Mapping::skipReasonText(Mapping::SkipReason(it.key())));
        skippedList.append(entry);
    }
    QVariantMap summary;
    summary.insert(QStringLiteral("files"), names);
    summary.insert(QStringLiteral("messages"), messages);
    summary.insert(QStringLiteral("newMessages"), newMessages);
    summary.insert(QStringLiteral("mms"), mms);
    summary.insert(QStringLiteral("newMms"), newMms);
    summary.insert(QStringLiteral("calls"), calls);
    summary.insert(QStringLiteral("newCalls"), newCalls);
    summary.insert(QStringLiteral("missingAttachments"), missingAttachments);
    summary.insert(QStringLiteral("skipped"), skippedList);
    summary.insert(QStringLiteral("firstTime"), first * 1000);
    summary.insert(QStringLiteral("lastTime"), last * 1000);
    analysis.summary = summary;
    analysis.ok = true;
    return analysis;
}

} // namespace

RestoreTask::RestoreTask(QObject *parent)
    : QObject(parent)
    , m_cancel(false)
{
    connect(&m_watcher, SIGNAL(finished()), this, SLOT(analysisDone()));
    removeOldStagingDirs();
}

RestoreTask::~RestoreTask()
{
    m_cancel = true;
    m_watcher.waitForFinished();
    if (m_watcher.future().isFinished() && m_watcher.future().resultCount() > 0) {
        const QString dir = m_watcher.result().stagingDir;
        if (!dir.isEmpty() && dir != m_stagingDir)
            QDir(dir).removeRecursively();
    }
    removeStagingDir();
}

void RestoreTask::removeStagingDir()
{
    if (!m_stagingDir.isEmpty()) {
        QDir(m_stagingDir).removeRecursively();
        m_stagingDir.clear();
        QDir().rmdir(HistoryStore::stagingRoot());  // only if no other restore uses it
    }
}

void RestoreTask::analyze(const QStringList &files)
{
    if (m_state == Analyzing || m_state == Restoring)
        return;
    m_cancel = false;
    m_events.clear();
    removeStagingDir();
    m_summary.clear();
    m_accounts.clear();
    m_result.clear();
    if (files.isEmpty()) {
        setState(Failed, tr("No backup file selected."));
        return;
    }
    setProgress(0, tr("Reading the backup"));
    setState(Analyzing);
    std::atomic<bool> *cancel = &m_cancel;
    RestoreTask *self = this;
    m_watcher.setFuture(QtConcurrent::run([files, cancel, self]() {
        return analyzeFiles(files, cancel, [self](qreal fraction, const QString &status) {
            QMetaObject::invokeMethod(self, "setProgress", Qt::QueuedConnection,
                                      Q_ARG(qreal, fraction), Q_ARG(QString, status));
        });
    }));
}

void RestoreTask::analysisDone()
{
    const Analysis analysis = m_watcher.result();
    m_stagingDir = analysis.stagingDir;
    if (analysis.cancelled || m_cancel) {
        removeStagingDir();
        setState(Idle);
        return;
    }
    if (!analysis.ok) {
        removeStagingDir();
        setState(Failed, analysis.error);
        return;
    }
    m_events = analysis.events;
    m_summary = analysis.summary;
    m_accounts.clear();
    for (const QString &account : SimAccounts::restoreTargets(analysis.accountsWithHistory)) {
        QVariantMap entry;
        entry.insert(QStringLiteral("localUid"), account);
        entry.insert(QStringLiteral("name"), SimAccounts::displayName(account));
        entry.insert(QStringLiteral("hasHistory"), analysis.accountsWithHistory.contains(account));
        m_accounts.append(entry);
    }
    setState(Ready);
}

void RestoreTask::restore(const QString &localUid, bool markAsRead)
{
    if (m_state != Ready)
        return;
    m_result.clear();
    if (m_events.isEmpty()) {
        m_result.insert(QStringLiteral("messages"), 0);
        m_result.insert(QStringLiteral("calls"), 0);
        m_result.insert(QStringLiteral("conversations"), 0);
        setState(Finished);
        return;
    }
    QList<HistoryEvent> events = m_events;
    if (markAsRead) {
        for (HistoryEvent &e : events) {
            if (e.isMessage())
                e.isRead = true;
        }
    }
    QString subscriberIdentity;
    if (HistoryStore::exists()) {
        HistoryStore store;
        QString error;
        if (store.open(&error))
            subscriberIdentity = store.subscriberIdentity(localUid);
    }
    if (!m_writer) {
        m_writer = new HistoryWriter(this);
        connect(m_writer, SIGNAL(progress(int,int)), this, SLOT(writerProgress(int,int)));
        connect(m_writer, SIGNAL(finished(bool,int,int,int,QString)),
                this, SLOT(writerFinished(bool,int,int,int,QString)));
    }
    m_cancel = false;
    setProgress(0, tr("Restoring"));
    setState(Restoring);
    m_writer->start(events, localUid, subscriberIdentity);
}

void RestoreTask::cancel()
{
    m_cancel = true;
    if (m_writer)
        m_writer->cancel();
}

void RestoreTask::reset()
{
    if (m_state == Analyzing || m_state == Restoring)
        return;
    m_events.clear();
    removeStagingDir();
    m_summary.clear();
    m_accounts.clear();
    m_result.clear();
    setState(Idle);
}

void RestoreTask::writerProgress(int done, int total)
{
    setProgress(total > 0 ? qreal(done) / qreal(total) : 1, tr("Restoring"));
}

void RestoreTask::writerFinished(bool ok, int messages, int calls, int conversations, const QString &error)
{
    m_result.insert(QStringLiteral("messages"), messages);
    m_result.insert(QStringLiteral("calls"), calls);
    m_result.insert(QStringLiteral("conversations"), conversations);
    m_events.clear();
    // What a cancelled or failed restore did not use
    removeStagingDir();
    setState(ok ? Finished : Failed, error);
}

void RestoreTask::setProgress(qreal progress, const QString &status)
{
    m_progress = progress;
    m_status = status;
    emit progressChanged();
}

void RestoreTask::setState(State state, const QString &error)
{
    m_error = error;
    m_state = state;
    emit stateChanged();
}
