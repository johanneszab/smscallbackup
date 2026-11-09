// SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
// SPDX-License-Identifier: MIT

#ifndef SETTINGS_H
#define SETTINGS_H

#include "schedule.h"

#include <QObject>
#include <QSettings>

class QFileSystemWatcher;
class QTimer;

// Persistent settings, in ~/.config/de.jzab/smscallbackup/ (the folder Sailjail allows).
// The app and the automatic backup (smscallbackup --scheduled, started by a systemd timer)
// share them: the app writes the choices, the automatic backup its results.
class Settings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString folder READ folder WRITE setFolder NOTIFY changed)
    Q_PROPERTY(QString defaultFolder READ defaultFolder CONSTANT)
    Q_PROPERTY(bool backupMessages READ backupMessages WRITE setBackupMessages NOTIFY changed)
    Q_PROPERTY(bool backupCalls READ backupCalls WRITE setBackupCalls NOTIFY changed)
    Q_PROPERTY(bool contactNames READ contactNames WRITE setContactNames NOTIFY changed)
    // Add new entries to sms.xml and calls.xml instead of writing new files each time
    Q_PROPERTY(bool incremental READ incremental WRITE setIncremental NOTIFY changed)
    Q_PROPERTY(bool markAsRead READ markAsRead WRITE setMarkAsRead NOTIFY changed)
    Q_PROPERTY(qint64 lastBackup READ lastBackup WRITE setLastBackup NOTIFY changed)

    // Automatic backups: Schedule::Frequency (0 off, 1 daily, 2 weekly, 3 monthly), time
    // of day in minutes after midnight, day of the week (1 = Monday) or of the month.
    Q_PROPERTY(int scheduleFrequency READ scheduleFrequency WRITE setScheduleFrequency NOTIFY changed)
    Q_PROPERTY(int scheduleTime READ scheduleTime WRITE setScheduleTime NOTIFY changed)
    Q_PROPERTY(int scheduleWeekday READ scheduleWeekday WRITE setScheduleWeekday NOTIFY changed)
    Q_PROPERTY(int scheduleMonthday READ scheduleMonthday WRITE setScheduleMonthday NOTIFY changed)
    // ms; in the past if a backup is due and waits for the phone to wake up; 0 if off
    Q_PROPERTY(qint64 nextScheduledBackup READ nextScheduledBackup NOTIFY changed)
    // The last automatic backup: when (ms, 0 if none), whether it worked, what it added
    Q_PROPERTY(qint64 lastScheduledBackup READ lastScheduledBackup NOTIFY changed)
    Q_PROPERTY(bool lastScheduledOk READ lastScheduledOk NOTIFY changed)
    Q_PROPERTY(QString lastScheduledError READ lastScheduledError NOTIFY changed)
    Q_PROPERTY(int lastScheduledMessages READ lastScheduledMessages NOTIFY changed)
    Q_PROPERTY(int lastScheduledCalls READ lastScheduledCalls NOTIFY changed)

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
    bool incremental() const;
    void setIncremental(bool on);
    bool markAsRead() const;
    void setMarkAsRead(bool on);
    qint64 lastBackup() const;
    void setLastBackup(qint64 msecs);

    Schedule schedule() const;
    int scheduleFrequency() const;
    void setScheduleFrequency(int frequency);
    int scheduleTime() const;
    void setScheduleTime(int minutes);
    int scheduleWeekday() const;
    void setScheduleWeekday(int day);
    int scheduleMonthday() const;
    void setScheduleMonthday(int day);
    // When the schedule was last changed (ms): backup times before it do not count.
    qint64 scheduleChanged() const;
    qint64 nextScheduledBackup() const;
    // Whether an automatic backup is due now.
    bool isScheduledBackupDue() const;

    qint64 lastScheduledBackup() const;
    bool lastScheduledOk() const;
    QString lastScheduledError() const;
    int lastScheduledMessages() const;
    int lastScheduledCalls() const;
    void setScheduledResult(qint64 when, bool ok, const QString &error, int messages, int calls);

    // Reads the file again, for changes made by the automatic backup.
    Q_INVOKABLE void reload();
    // Reloads by itself when the automatic backup changes the file (for the app).
    void watchFile();
    // The systemd timer only starts smscallbackup --scheduled while this file exists.
    void updateScheduleMarker();

    static QString configDir();
    static QString scheduleMarkerPath();

signals:
    void changed();
    // A backup was made by another process (the automatic backup).
    void backupsChangedElsewhere();

private slots:
    void fileChanged();

private:
    void set(const char *key, const QVariant &value);
    void setScheduleValue(const char *key, int value);

    QSettings m_settings;
    QFileSystemWatcher *m_watcher = nullptr;
    QTimer *m_reloadTimer = nullptr;
};

#endif // SETTINGS_H
