// SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
// SPDX-License-Identifier: MIT

#ifndef SCHEDULE_H
#define SCHEDULE_H

#include <QDate>
#include <QDateTime>

// When automatic backups run: every day, once a week or once a month, at a time of day
// (local time). A backup is due when a backup time has passed since the last run, so a
// backup the phone missed (switched off, in deep sleep) runs as soon as it can.
struct Schedule
{
    enum Frequency {
        Off = 0,
        Daily,
        Weekly,
        Monthly
    };

    int frequency = Off;
    int minutes = 3 * 60;   // time of day, minutes after midnight
    int weekday = 1;        // weekly: 1 = Monday ... 7 = Sunday
    int monthday = 1;       // monthly: 1 ... 31; months without that day use their last day

    bool isEnabled() const { return frequency >= Daily && frequency <= Monthly; }

    // Whether the schedule has a backup on that day.
    bool isBackupDay(const QDate &date) const;
    // The backup time on a day (local time). A time that does not exist on that day
    // because the clocks go forward is moved by an hour.
    QDateTime backupTime(const QDate &date) const;
    // The last backup time at or before `now`; invalid if the schedule is off.
    QDateTime previous(const QDateTime &now) const;
    // The first backup time after `now`; invalid if the schedule is off.
    QDateTime next(const QDateTime &now) const;
    // Whether a backup time has passed after `since` (the last run, or when the schedule
    // was changed). An invalid `since` counts as "never".
    bool isDue(const QDateTime &now, const QDateTime &since) const;
};

#endif // SCHEDULE_H
