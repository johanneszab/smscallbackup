// SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
// SPDX-License-Identifier: MIT

#include "settings.h"

#include <QStandardPaths>

namespace {

QString settingsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
            + QStringLiteral("/smscallbackup.conf");
}

} // namespace

Settings::Settings(QObject *parent)
    : QObject(parent)
    , m_settings(settingsPath(), QSettings::IniFormat)
{
}

QString Settings::folder() const
{
    const QString folder = m_settings.value(QStringLiteral("folder")).toString();
    return folder.isEmpty() ? defaultFolder() : folder;
}

void Settings::setFolder(const QString &folder)
{
    set("folder", folder == defaultFolder() ? QString() : folder);
}

QString Settings::defaultFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/smscallbackup");
}

bool Settings::backupMessages() const
{
    return m_settings.value(QStringLiteral("backupMessages"), true).toBool();
}

void Settings::setBackupMessages(bool on)
{
    set("backupMessages", on);
}

bool Settings::backupCalls() const
{
    return m_settings.value(QStringLiteral("backupCalls"), true).toBool();
}

void Settings::setBackupCalls(bool on)
{
    set("backupCalls", on);
}

bool Settings::contactNames() const
{
    return m_settings.value(QStringLiteral("contactNames"), true).toBool();
}

void Settings::setContactNames(bool on)
{
    set("contactNames", on);
}

bool Settings::markAsRead() const
{
    return m_settings.value(QStringLiteral("markAsRead"), true).toBool();
}

void Settings::setMarkAsRead(bool on)
{
    set("markAsRead", on);
}

qint64 Settings::lastBackup() const
{
    return m_settings.value(QStringLiteral("lastBackup"), 0).toLongLong();
}

void Settings::setLastBackup(qint64 msecs)
{
    set("lastBackup", msecs);
}

void Settings::set(const char *key, const QVariant &value)
{
    const QString name = QLatin1String(key);
    if (m_settings.value(name) == value)
        return;
    if (value.isNull() || (value.type() == QVariant::String && value.toString().isEmpty()))
        m_settings.remove(name);
    else
        m_settings.setValue(name, value);
    m_settings.sync();
    emit changed();
}
