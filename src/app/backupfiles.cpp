// SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
// SPDX-License-Identifier: MIT

#include "backupfiles.h"

#include "sbrreader.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <algorithm>

BackupFiles::BackupFiles(QObject *parent)
    : QObject(parent)
{
}

QVariantMap BackupFiles::info(const QString &path) const
{
    const QFileInfo file(path);
    const SbrReader::Info info = SbrReader::probe(path);
    QVariantMap map;
    map.insert(QStringLiteral("ok"), info.ok);
    map.insert(QStringLiteral("error"), info.error);
    map.insert(QStringLiteral("kind"), info.kind == SbrReader::MessagesKind ? QStringLiteral("messages")
                                     : info.kind == SbrReader::CallsKind ? QStringLiteral("calls") : QString());
    map.insert(QStringLiteral("count"), info.count);
    map.insert(QStringLiteral("date"), info.backupDate > 0 ? info.backupDate
                                                           : file.lastModified().toMSecsSinceEpoch());
    map.insert(QStringLiteral("compressed"), info.compressed);
    map.insert(QStringLiteral("name"), file.fileName());
    map.insert(QStringLiteral("size"), file.size());
    map.insert(QStringLiteral("backupSet"), info.backupSet);
    return map;
}

QVariantList BackupFiles::list(const QString &folder) const
{
    QDir dir(folder);
    const QFileInfoList entries = dir.entryInfoList(QStringList() << QStringLiteral("*.xml") << QStringLiteral("*.zip"),
                                                    QDir::Files | QDir::Readable, QDir::Name);
    QMap<QString, QVariantMap> sets;
    for (const QFileInfo &entry : entries) {
        const QVariantMap file = info(entry.absoluteFilePath());
        if (!file.value(QStringLiteral("ok")).toBool())
            continue;
        QString key = file.value(QStringLiteral("backupSet")).toString();
        if (key.isEmpty())
            key = entry.absoluteFilePath();
        QVariantMap set = sets.value(key);
        if (set.isEmpty()) {
            set.insert(QStringLiteral("messages"), -1);
            set.insert(QStringLiteral("calls"), -1);
            set.insert(QStringLiteral("date"), qint64(0));
            set.insert(QStringLiteral("size"), qint64(0));
            set.insert(QStringLiteral("compressed"), false);
        }
        QStringList files = set.value(QStringLiteral("files")).toStringList();
        QStringList names = set.value(QStringLiteral("names")).toStringList();
        files.append(entry.absoluteFilePath());
        names.append(entry.fileName());
        set.insert(QStringLiteral("files"), files);
        set.insert(QStringLiteral("names"), names);
        const QString kind = file.value(QStringLiteral("kind")).toString();
        const int count = file.value(QStringLiteral("count")).toInt();
        if (count >= 0) {
            const int previous = qMax(0, set.value(kind).toInt());
            set.insert(kind, previous + count);
        }
        set.insert(QStringLiteral("date"), qMax(set.value(QStringLiteral("date")).toLongLong(),
                                                file.value(QStringLiteral("date")).toLongLong()));
        set.insert(QStringLiteral("size"), set.value(QStringLiteral("size")).toLongLong() + entry.size());
        if (file.value(QStringLiteral("compressed")).toBool())
            set.insert(QStringLiteral("compressed"), true);
        sets.insert(key, set);
    }
    QVariantList result;
    for (const QVariantMap &set : sets)
        result.append(set);
    std::sort(result.begin(), result.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("date")).toLongLong() > b.toMap().value(QStringLiteral("date")).toLongLong();
    });
    return result;
}

bool BackupFiles::remove(const QStringList &paths) const
{
    bool ok = true;
    for (const QString &path : paths)
        ok = QFile::remove(path) && ok;
    return ok;
}

QString BackupFiles::displayPath(const QString &path) const
{
    const QString home = QDir::homePath();
    if (path == home)
        return QStringLiteral("~");
    if (path.startsWith(home + QLatin1Char('/')))
        return QStringLiteral("~") + path.mid(home.size());
    return path;
}
