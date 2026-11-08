// SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
// SPDX-License-Identifier: MIT

#ifndef BACKUPFILES_H
#define BACKUPFILES_H

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// Finds the backups in a folder. The SMS and call log files of one backup run (same
// backup_set, as written by this app and by SMS Backup & Restore) are shown as one entry.
class BackupFiles : public QObject
{
    Q_OBJECT

public:
    explicit BackupFiles(QObject *parent = nullptr);

    // Newest first: [{ date (ms), files: [paths], names: [file names], messages, calls,
    // compressed, size (bytes) }]; messages/calls are -1 if the files do not say.
    Q_INVOKABLE QVariantList list(const QString &folder) const;
    // { ok, error, kind ("messages"/"calls"), count, date, compressed, name, size }
    Q_INVOKABLE QVariantMap info(const QString &path) const;
    // Deletes the files of a backup.
    Q_INVOKABLE bool remove(const QStringList &paths) const;
    // "~/Documents/smscallbackup" for a path in the home folder.
    Q_INVOKABLE QString displayPath(const QString &path) const;
};

#endif // BACKUPFILES_H
