// SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
// SPDX-License-Identifier: MIT

#include "cli.h"

#include "backuptask.h"
#include "mapping.h"
#include "restoretask.h"
#include "sbrreader.h"
#include "settings.h"
#include "simaccounts.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QTextStream>
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

int runBackup(QCoreApplication &app, const QString &folder, bool messages, bool calls, bool names)
{
    BackupTask task;
    int rc = 1;
    QObject::connect(&task, &BackupTask::finished,
                     [&](bool ok, const QString &message, const QStringList &files, int m, int c) {
        if (ok) {
            out() << "Backed up " << m << " messages and " << c << " calls:" << endl;
            for (const QString &file : files)
                out() << "  " << file << endl;
            Settings settings;
            settings.setLastBackup(QDateTime::currentMSecsSinceEpoch());
            rc = 0;
        } else {
            err() << "Backup failed: " << message << endl;
        }
        app.quit();
    });
    out() << "Backing up to " << folder << endl;
    task.start(folder, messages, calls, names);
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
            out() << "In the backup: " << s.value(QStringLiteral("messages")).toInt() << " SMS, "
                  << s.value(QStringLiteral("calls")).toInt() << " calls" << endl;
            out() << "Not on the phone yet: " << s.value(QStringLiteral("newMessages")).toInt() << " SMS, "
                  << s.value(QStringLiteral("newCalls")).toInt() << " calls (from "
                  << date(s.value(QStringLiteral("firstTime")).toLongLong()) << " to "
                  << date(s.value(QStringLiteral("lastTime")).toLongLong()) << ")" << endl;
            if (s.value(QStringLiteral("mms")).toInt() > 0)
                out() << "Not restored: " << s.value(QStringLiteral("mms")).toInt() << " MMS (not supported yet)" << endl;
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
            out() << "Restored " << r.value(QStringLiteral("messages")).toInt() << " SMS ("
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
                      << " SMS, " << task.result().value(QStringLiteral("calls")).toInt() << " calls" << endl;
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
                || !std::strcmp(arg, "--help") || !std::strcmp(arg, "-h") || !std::strcmp(arg, "--version"))
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
    parser.addOptions({ backup, restore, info, messagesOnly, callsOnly, noNames, sim, keepUnread, dryRun });
    parser.addPositionalArgument(QStringLiteral("paths"), QStringLiteral("Backup folder or files."), QStringLiteral("[paths...]"));
    parser.process(app);

    const QStringList paths = parser.positionalArguments();
    if (parser.isSet(info))
        return runInfo(paths);
    if (parser.isSet(backup)) {
        const QString folder = paths.isEmpty() ? Settings().folder() : QDir(paths.first()).absolutePath();
        return runBackup(app, folder, !parser.isSet(callsOnly), !parser.isSet(messagesOnly), !parser.isSet(noNames));
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
