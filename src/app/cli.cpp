// SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
// SPDX-License-Identifier: MIT

#include "cli.h"

#include "backuptask.h"
#include "mapping.h"
#include "restoretask.h"
#include "sbrreader.h"
#include "settings.h"
#include "simaccounts.h"

#include <sailfishapp.h>

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QTextStream>
#include <QTranslator>
#include <cstdio>
#include <cstring>

namespace {

QTextStream &out()
{
    static QTextStream stream(stdout);
    return stream;
}

QTextStream &err()
{
    static QTextStream stream(stderr);
    return stream;
}

QString date(qint64 msecs)
{
    return msecs > 0 ? QDateTime::fromMSecsSinceEpoch(msecs).toString(QStringLiteral("yyyy-MM-dd HH:mm"))
                     : QStringLiteral("-");
}

int runInfo(const QStringList &files)
{
    int rc = 0;
    for (const QString &path : files) {
        const SbrReader::Info info = SbrReader::probe(path);
        if (!info.ok) {
            err() << info.error << endl;
            rc = 1;
            continue;
        }
        out() << path << ": " << (info.kind == SbrReader::MessagesKind ? "messages" : "call log")
              << ", " << info.count << " entries, made " << date(info.backupDate)
              << (info.compressed ? ", compressed" : "") << endl;
    }
    return rc;
}

void printBackupResult(const QVariantMap &result)
{
    const int messages = result.value(QStringLiteral("messages")).toInt();
    const int mms = result.value(QStringLiteral("mms")).toInt();
    const int calls = result.value(QStringLiteral("calls")).toInt();
    if (result.value(QStringLiteral("incremental")).toBool()) {
        out() << "Added " << messages << " messages (" << mms << " MMS) and " << calls << " calls; the files now have "
              << result.value(QStringLiteral("messagesTotal")).toInt() << " messages and "
              << result.value(QStringLiteral("callsTotal")).toInt() << " calls." << endl;
    } else {
        out() << "Backed up " << messages << " messages (" << mms << " MMS) and " << calls << " calls." << endl;
    }
    const QStringList files = result.value(QStringLiteral("files")).toStringList();
    for (const QString &file : files)
        out() << "  " << file << endl;
    if (files.isEmpty())
        out() << "  Nothing new, the files were left as they are." << endl;
    if (result.value(QStringLiteral("missingParts")).toInt() > 0)
        out() << "Attachments whose file is missing on the phone: "
              << result.value(QStringLiteral("missingParts")).toInt() << endl;
}

int runBackup(QCoreApplication &app, const QString &folder, bool messages, bool calls, bool names, bool incremental)
{
    BackupTask task;
    int rc = 1;
    QObject::connect(&task, &BackupTask::finished,
                     [&](bool ok, const QString &message, const QVariantMap &result) {
        if (ok) {
            printBackupResult(result);
            Settings settings;
            settings.setLastBackup(QDateTime::currentMSecsSinceEpoch());
            rc = 0;
        } else {
            err() << "Backup failed: " << message << endl;
        }
        app.quit();
    });
    out() << "Backing up to " << folder << (incremental ? " (adding to sms.xml and calls.xml)" : "") << endl;
    task.start(folder, messages, calls, names, incremental);
    if (task.isRunning())
        app.exec();
    return rc;
}

// A notification in the Events view, e.g. when an automatic backup failed.
void notify(const QString &summary, const QString &body)
{
    QVariantMap hints;
    hints.insert(QStringLiteral("x-nemo-preview-summary"), summary);
    hints.insert(QStringLiteral("x-nemo-preview-body"), body);
    hints.insert(QStringLiteral("urgency"), QVariant::fromValue<uchar>(1));
    QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.Notifications"),
                                                       QStringLiteral("/org/freedesktop/Notifications"),
                                                       QStringLiteral("org.freedesktop.Notifications"),
                                                       QStringLiteral("Notify"));
    call << QCoreApplication::translate("Cli", "SMS & Call Backup") << uint(0)
         << QStringLiteral("/usr/share/icons/hicolor/86x86/apps/smscallbackup.png")
         << summary << body << QStringList() << hints << int(-1);
    QDBusConnection::sessionBus().call(call, QDBus::Block, 5000);
}

// The automatic backup, started every 15 minutes by smscallbackup-scheduled.timer while
// automatic backups are on: backs up if a backup time has passed since the last run.
int runScheduled(QCoreApplication &app, bool force)
{
    // Messages and the notification in the phone's language, like the app.
    static QTranslator english;
    static QTranslator translator;
    const QString translations = SailfishApp::pathTo(QStringLiteral("translations")).toLocalFile();
    if (english.load(QStringLiteral("smscallbackup-en"), translations))
        app.installTranslator(&english);
    if (translator.load(QLocale(), QStringLiteral("smscallbackup"), QStringLiteral("-"), translations))
        app.installTranslator(&translator);

    Settings settings;
    const Schedule schedule = settings.schedule();
    if (!force && !schedule.isEnabled()) {
        out() << "Automatic backups are off." << endl;
        return 0;
    }
    if (!force && !settings.isScheduledBackupDue()) {
        const qint64 next = settings.nextScheduledBackup();
        out() << "No backup due, the next one is at " << date(next) << "." << endl;
        return 0;
    }

    const qint64 started = QDateTime::currentMSecsSinceEpoch();
    BackupTask task;
    int rc = 1;
    QObject::connect(&task, &BackupTask::finished,
                     [&](bool ok, const QString &message, const QVariantMap &result) {
        if (!ok && result.value(QStringLiteral("busy")).toBool()) {
            // The app is backing up: the next timer run tries again.
            out() << "Another backup is running, trying again later." << endl;
            rc = 0;
            app.quit();
            return;
        }
        settings.setScheduledResult(started, ok, message, result.value(QStringLiteral("messages")).toInt(),
                                    result.value(QStringLiteral("calls")).toInt());
        if (ok) {
            printBackupResult(result);
            rc = 0;
        } else {
            err() << "Automatic backup failed: " << message << endl;
            notify(QCoreApplication::translate("Cli", "Automatic backup failed"), message);
        }
        app.quit();
    });
    out() << "Automatic backup to " << settings.folder() << endl;
    task.start(settings.folder(), settings.backupMessages(), settings.backupCalls(), settings.contactNames(),
               settings.incremental());
    if (task.isRunning())
        app.exec();
    return rc;
}

int runRestore(QCoreApplication &app, const QStringList &files, int sim, bool keepUnread, bool dryRun)
{
    RestoreTask task;
    int rc = 1;
    QObject::connect(&task, &RestoreTask::stateChanged, [&]() {
        switch (task.state()) {
        case RestoreTask::Ready: {
            const QVariantMap s = task.summary();
            out() << "In the backup: " << s.value(QStringLiteral("messages")).toInt() << " messages ("
                  << s.value(QStringLiteral("mms")).toInt() << " MMS), "
                  << s.value(QStringLiteral("calls")).toInt() << " calls" << endl;
            out() << "Not on the phone yet: " << s.value(QStringLiteral("newMessages")).toInt() << " messages ("
                  << s.value(QStringLiteral("newMms")).toInt() << " MMS), "
                  << s.value(QStringLiteral("newCalls")).toInt() << " calls (from "
                  << date(s.value(QStringLiteral("firstTime")).toLongLong()) << " to "
                  << date(s.value(QStringLiteral("lastTime")).toLongLong()) << ")" << endl;
            if (s.value(QStringLiteral("missingAttachments")).toInt() > 0)
                out() << "Attachments not in the backup: " << s.value(QStringLiteral("missingAttachments")).toInt() << endl;
            for (const QVariant &v : s.value(QStringLiteral("skipped")).toList()) {
                const QVariantMap entry = v.toMap();
                out() << "Skipped: " << entry.value(QStringLiteral("count")).toInt() << " "
                      << entry.value(QStringLiteral("reason")).toString() << endl;
            }
            QString account;
            if (sim > 0) {
                account = Mapping::ringAccount(sim);
            } else if (!task.accounts().isEmpty()) {
                account = task.accounts().first().toMap().value(QStringLiteral("localUid")).toString();
            }
            out() << "Account: " << SimAccounts::displayName(account) << " (" << account << ")" << endl;
            if (dryRun) {
                out() << "Dry run, nothing changed." << endl;
                rc = 0;
                app.quit();
            } else {
                task.restore(account, !keepUnread);
            }
            break;
        }
        case RestoreTask::Finished: {
            const QVariantMap r = task.result();
            out() << "Restored " << r.value(QStringLiteral("messages")).toInt() << " messages ("
                  << r.value(QStringLiteral("conversations")).toInt() << " new conversations) and "
                  << r.value(QStringLiteral("calls")).toInt() << " calls." << endl;
            rc = 0;
            app.quit();
            break;
        }
        case RestoreTask::Failed:
            err() << "Restore failed: " << task.error() << endl;
            if (task.result().value(QStringLiteral("messages")).toInt() + task.result().value(QStringLiteral("calls")).toInt() > 0)
                err() << "Restored before the failure: " << task.result().value(QStringLiteral("messages")).toInt()
                      << " messages, " << task.result().value(QStringLiteral("calls")).toInt() << " calls" << endl;
            app.quit();
            break;
        default:
            break;
        }
    });
    task.analyze(files);
    if (task.state() == RestoreTask::Analyzing)
        app.exec();
    return rc;
}

} // namespace

namespace Cli {

bool wanted(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (!std::strcmp(arg, "--backup") || !std::strcmp(arg, "--restore") || !std::strcmp(arg, "--info")
                || !std::strcmp(arg, "--scheduled") || !std::strcmp(arg, "--help") || !std::strcmp(arg, "-h")
                || !std::strcmp(arg, "--version"))
            return true;
    }
    return false;
}

int run(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("de.jzab"));
    app.setApplicationName(QStringLiteral("smscallbackup"));
    app.setApplicationVersion(QStringLiteral(APP_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
            "Back up and restore SMS and the call log in the XML format of SMS Backup & Restore."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption backup(QStringLiteral("backup"),
            QStringLiteral("Back up to the folder given as argument (default: the app's backup folder)."));
    const QCommandLineOption restore(QStringLiteral("restore"),
            QStringLiteral("Restore the backup files given as arguments."));
    const QCommandLineOption info(QStringLiteral("info"), QStringLiteral("Show what the given backup files contain."));
    const QCommandLineOption messagesOnly(QStringLiteral("messages-only"), QStringLiteral("Back up only messages."));
    const QCommandLineOption callsOnly(QStringLiteral("calls-only"), QStringLiteral("Back up only the call log."));
    const QCommandLineOption noNames(QStringLiteral("no-contact-names"),
            QStringLiteral("Do not write contact names into the backup."));
    const QCommandLineOption sim(QStringLiteral("sim"),
            QStringLiteral("Attach restored history to SIM <n> (default: the SIM used most recently)."),
            QStringLiteral("n"));
    const QCommandLineOption keepUnread(QStringLiteral("keep-unread"),
            QStringLiteral("Keep unread messages unread (default: restore everything as read)."));
    const QCommandLineOption dryRun(QStringLiteral("dry-run"),
            QStringLiteral("Only show what would be restored."));
    const QCommandLineOption incremental(QStringLiteral("incremental"),
            QStringLiteral("Add what is new to sms.xml and calls.xml (default: as set in the app)."));
    const QCommandLineOption newFiles(QStringLiteral("new-files"),
            QStringLiteral("Write new files sms-<time>.xml and calls-<time>.xml (default: as set in the app)."));
    const QCommandLineOption scheduled(QStringLiteral("scheduled"),
            QStringLiteral("Make the automatic backup if one is due, with the app's settings (used by the systemd timer)."));
    const QCommandLineOption force(QStringLiteral("force"),
            QStringLiteral("With --scheduled: back up now, even if no backup is due."));
    parser.addOptions({ backup, restore, info, messagesOnly, callsOnly, noNames, incremental, newFiles, sim,
                        keepUnread, dryRun, scheduled, force });
    parser.addPositionalArgument(QStringLiteral("paths"), QStringLiteral("Backup folder or files."), QStringLiteral("[paths...]"));
    parser.process(app);

    const QStringList paths = parser.positionalArguments();
    if (parser.isSet(info))
        return runInfo(paths);
    if (parser.isSet(scheduled))
        return runScheduled(app, parser.isSet(force));
    if (parser.isSet(backup)) {
        const Settings settings;
        const QString folder = paths.isEmpty() ? settings.folder() : QDir(paths.first()).absolutePath();
        const bool addToFiles = parser.isSet(newFiles) ? false : (parser.isSet(incremental) || settings.incremental());
        return runBackup(app, folder, !parser.isSet(callsOnly), !parser.isSet(messagesOnly), !parser.isSet(noNames),
                         addToFiles);
    }
    if (parser.isSet(restore)) {
        QStringList files;
        for (const QString &path : paths)
            files.append(QFileInfo(path).absoluteFilePath());
        bool ok = true;
        const int slot = parser.isSet(sim) ? parser.value(sim).toInt(&ok) : 0;
        if (!ok || slot < 0) {
            err() << "Invalid SIM number: " << parser.value(sim) << endl;
            return 2;
        }
        return runRestore(app, files, slot, parser.isSet(keepUnread), parser.isSet(dryRun));
    }
    parser.showHelp(2);
    return 2;
}

} // namespace Cli
