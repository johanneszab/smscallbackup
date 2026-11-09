// SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
// SPDX-License-Identifier: MIT

#include "settings.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileSystemWatcher>
#include <QStandardPaths>
#include <QTimer>

namespace {

QString settingsPath()
{
    return Settings::configDir() + QStringLiteral("/smscallbackup.conf");
}

QDateTime fromMSecs(qint64 msecs)
{
    return msecs > 0 ? QDateTime::fromMSecsSinceEpoch(msecs) : QDateTime();
}

} // namespace

Settings::Settings(QObject *parent)
    : QObject(parent)
    , m_settings(settingsPath(), QSettings::IniFormat)
{
}

QString Settings::configDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
}

QString Settings::scheduleMarkerPath()
{
    return configDir() + QStringLiteral("/schedule-active");
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

bool Settings::incremental() const
{
    return m_settings.value(QStringLiteral("incremental"), true).toBool();
}

void Settings::setIncremental(bool on)
{
    set("incremental", on);
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

Schedule Settings::schedule() const
{
    Schedule schedule;
    schedule.frequency = scheduleFrequency();
    schedule.minutes = scheduleTime();
    schedule.weekday = scheduleWeekday();
    schedule.monthday = scheduleMonthday();
    return schedule;
}

int Settings::scheduleFrequency() const
{
    const int frequency = m_settings.value(QStringLiteral("scheduleFrequency"), int(Schedule::Off)).toInt();
    return (frequency >= Schedule::Off && frequency <= Schedule::Monthly) ? frequency : int(Schedule::Off);
}

void Settings::setScheduleFrequency(int frequency)
{
    setScheduleValue("scheduleFrequency", qBound(int(Schedule::Off), frequency, int(Schedule::Monthly)));
    updateScheduleMarker();
}

int Settings::scheduleTime() const
{
    return qBound(0, m_settings.value(QStringLiteral("scheduleTime"), 3 * 60).toInt(), 24 * 60 - 1);
}

void Settings::setScheduleTime(int minutes)
{
    setScheduleValue("scheduleTime", qBound(0, minutes, 24 * 60 - 1));
}

int Settings::scheduleWeekday() const
{
    return qBound(1, m_settings.value(QStringLiteral("scheduleWeekday"), 1).toInt(), 7);
}

void Settings::setScheduleWeekday(int day)
{
    setScheduleValue("scheduleWeekday", qBound(1, day, 7));
}

int Settings::scheduleMonthday() const
{
    return qBound(1, m_settings.value(QStringLiteral("scheduleMonthday"), 1).toInt(), 31);
}

void Settings::setScheduleMonthday(int day)
{
    setScheduleValue("scheduleMonthday", qBound(1, day, 31));
}

qint64 Settings::scheduleChanged() const
{
    return m_settings.value(QStringLiteral("scheduleChanged"), 0).toLongLong();
}

bool Settings::isScheduledBackupDue() const
{
    const QDateTime since = fromMSecs(qMax(lastScheduledBackup(), scheduleChanged()));
    return schedule().isDue(QDateTime::currentDateTime(), since);
}

qint64 Settings::nextScheduledBackup() const
{
    const Schedule s = schedule();
    if (!s.isEnabled())
        return 0;
    const QDateTime now = QDateTime::currentDateTime();
    if (isScheduledBackupDue())
        return s.previous(now).toMSecsSinceEpoch();
    const QDateTime next = s.next(now);
    return next.isValid() ? next.toMSecsSinceEpoch() : 0;
}

qint64 Settings::lastScheduledBackup() const
{
    return m_settings.value(QStringLiteral("lastScheduledBackup"), 0).toLongLong();
}

bool Settings::lastScheduledOk() const
{
    return m_settings.value(QStringLiteral("lastScheduledOk"), true).toBool();
}

QString Settings::lastScheduledError() const
{
    return m_settings.value(QStringLiteral("lastScheduledError")).toString();
}

int Settings::lastScheduledMessages() const
{
    return m_settings.value(QStringLiteral("lastScheduledMessages"), 0).toInt();
}

int Settings::lastScheduledCalls() const
{
    return m_settings.value(QStringLiteral("lastScheduledCalls"), 0).toInt();
}

void Settings::setScheduledResult(qint64 when, bool ok, const QString &error, int messages, int calls)
{
    m_settings.setValue(QStringLiteral("lastScheduledBackup"), when);
    m_settings.setValue(QStringLiteral("lastScheduledOk"), ok);
    if (error.isEmpty())
        m_settings.remove(QStringLiteral("lastScheduledError"));
    else
        m_settings.setValue(QStringLiteral("lastScheduledError"), error);
    m_settings.setValue(QStringLiteral("lastScheduledMessages"), messages);
    m_settings.setValue(QStringLiteral("lastScheduledCalls"), calls);
    if (ok)
        m_settings.setValue(QStringLiteral("lastBackup"), when);
    m_settings.sync();
    emit changed();
}

void Settings::reload()
{
    const qint64 lastBackupBefore = lastBackup();
    const qint64 scheduledBefore = lastScheduledBackup();
    m_settings.sync();
    emit changed();
    if (lastBackup() != lastBackupBefore || lastScheduledBackup() != scheduledBefore)
        emit backupsChangedElsewhere();
}

void Settings::watchFile()
{
    if (m_watcher)
        return;
    QDir().mkpath(configDir());
    // QSettings replaces the file when it saves, so the folder is watched.
    m_watcher = new QFileSystemWatcher(QStringList() << configDir(), this);
    m_reloadTimer = new QTimer(this);
    m_reloadTimer->setSingleShot(true);
    m_reloadTimer->setInterval(500);
    connect(m_watcher, SIGNAL(directoryChanged(QString)), this, SLOT(fileChanged()));
    connect(m_watcher, SIGNAL(fileChanged(QString)), this, SLOT(fileChanged()));
    connect(m_reloadTimer, SIGNAL(timeout()), this, SLOT(reload()));
}

void Settings::fileChanged()
{
    if (m_reloadTimer)
        m_reloadTimer->start();
}

void Settings::updateScheduleMarker()
{
    const QString path = scheduleMarkerPath();
    if (schedule().isEnabled()) {
        if (!QFile::exists(path)) {
            QDir().mkpath(configDir());
            QFile marker(path);
            if (marker.open(QIODevice::WriteOnly))
                marker.write("Automatic backups are on (see smscallbackup.conf).\n");
        }
    } else if (QFile::exists(path)) {
        QFile::remove(path);
    }
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

void Settings::setScheduleValue(const char *key, int value)
{
    const QString name = QLatin1String(key);
    if (m_settings.contains(name) && m_settings.value(name).toInt() == value)
        return;
    m_settings.setValue(name, value);
    // Backup times that passed before the change do not count: a backup set up at noon
    // for 03:00 first runs at 03:00 the next night.
    m_settings.setValue(QStringLiteral("scheduleChanged"), QDateTime::currentMSecsSinceEpoch());
    m_settings.sync();
    emit changed();
}
