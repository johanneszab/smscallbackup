// SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
// SPDX-License-Identifier: MIT

#include "backupfiles.h"
#include "backuptask.h"
#include "cli.h"
#include "restoretask.h"
#include "settings.h"

#include <sailfishapp.h>

#include <QGuiApplication>
#include <QLocale>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickView>
#include <QScopedPointer>
#include <QTranslator>
#include <QtQml>

int main(int argc, char *argv[])
{
    if (Cli::wanted(argc, argv))
        return Cli::run(argc, argv);

    QScopedPointer<QGuiApplication> app(SailfishApp::application(argc, argv));
    // Must match OrganizationName and ApplicationName in the desktop file (Sailjail).
    app->setOrganizationName(QStringLiteral("de.jzab"));
    app->setApplicationName(QStringLiteral("smscallbackup"));
    app->setApplicationVersion(QStringLiteral(APP_VERSION));

    // English first (it only has the plural forms), the user's language on top of it.
    const QString translations = SailfishApp::pathTo(QStringLiteral("translations")).toLocalFile();
    QTranslator english;
    if (english.load(QStringLiteral("smscallbackup-en"), translations))
        app->installTranslator(&english);
    QTranslator translator;
    if (translator.load(QLocale(), QStringLiteral("smscallbackup"), QStringLiteral("-"), translations))
        app->installTranslator(&translator);

    qmlRegisterUncreatableType<BackupTask>("SmsCallBackup", 1, 0, "BackupTask", QStringLiteral("Provided by the app"));
    qmlRegisterUncreatableType<RestoreTask>("SmsCallBackup", 1, 0, "RestoreTask", QStringLiteral("Provided by the app"));

    Settings settings;
    // The automatic backup runs in another process and writes its results to the settings.
    settings.watchFile();
    settings.updateScheduleMarker();
    BackupFiles backupFiles;
    BackupTask backupTask;
    RestoreTask restoreTask;

    QScopedPointer<QQuickView> view(SailfishApp::createView());
    QQmlContext *context = view->rootContext();
    context->setContextProperty(QStringLiteral("appVersion"), QStringLiteral(APP_VERSION));
    context->setContextProperty(QStringLiteral("appSettings"), &settings);
    context->setContextProperty(QStringLiteral("backupFiles"), &backupFiles);
    context->setContextProperty(QStringLiteral("backupTask"), &backupTask);
    context->setContextProperty(QStringLiteral("restoreTask"), &restoreTask);
    view->setSource(SailfishApp::pathToMainQml());
    view->show();
    return app->exec();
}
