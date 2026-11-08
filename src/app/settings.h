// SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
// SPDX-License-Identifier: MIT

#ifndef SETTINGS_H
#define SETTINGS_H

#include <QObject>
#include <QSettings>

// Persistent settings, in ~/.config/de.jzab/smscallbackup/ (the folder Sailjail allows).
class Settings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString folder READ folder WRITE setFolder NOTIFY changed)
    Q_PROPERTY(QString defaultFolder READ defaultFolder CONSTANT)
    Q_PROPERTY(bool backupMessages READ backupMessages WRITE setBackupMessages NOTIFY changed)
    Q_PROPERTY(bool backupCalls READ backupCalls WRITE setBackupCalls NOTIFY changed)
    Q_PROPERTY(bool contactNames READ contactNames WRITE setContactNames NOTIFY changed)
    Q_PROPERTY(bool markAsRead READ markAsRead WRITE setMarkAsRead NOTIFY changed)
    Q_PROPERTY(qint64 lastBackup READ lastBackup WRITE setLastBackup NOTIFY changed)

public:
    explicit Settings(QObject *parent = nullptr);

    QString folder() const;
    void setFolder(const QString &folder);
    static QString defaultFolder();
    bool backupMessages() const;
    void setBackupMessages(bool on);
    bool backupCalls() const;
    void setBackupCalls(bool on);
    bool contactNames() const;
    void setContactNames(bool on);
    bool markAsRead() const;
    void setMarkAsRead(bool on);
    qint64 lastBackup() const;
    void setLastBackup(qint64 msecs);

signals:
    void changed();

private:
    void set(const char *key, const QVariant &value);

    QSettings m_settings;
};

#endif // SETTINGS_H
