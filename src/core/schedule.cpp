// SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
// SPDX-License-Identifier: MIT

#include "schedule.h"

#include <QTime>

namespace {

// Far enough to find the backup day of any month.
const int SearchDays = 62;

} // namespace

bool Schedule::isBackupDay(const QDate &date) const
{
    switch (frequency) {
    case Daily:
        return true;
    case Weekly:
        return date.dayOfWeek() == qBound(1, weekday, 7);
    case Monthly:
        return date.day() == qMin(qBound(1, monthday, 31), date.daysInMonth());
    default:
        return false;
    }
}

QDateTime Schedule::backupTime(const QDate &date) const
{
    const int clamped = qBound(0, minutes, 24 * 60 - 1);
    const QTime time(clamped / 60, clamped % 60);
    QDateTime when(date, time);
    if (!when.isValid() || when.time() != time) {
        // In the gap of a daylight saving time change (e.g. 02:30 on the day the clocks
        // go from 02:00 to 03:00): an hour later.
        const QDateTime later(date, time.addSecs(3600));
        if (later.isValid())
            when = later;
    }
    return when;
}

QDateTime Schedule::previous(const QDateTime &now) const
{
    if (!isEnabled() || !now.isValid())
        return QDateTime();
    const QDate today = now.toLocalTime().date();
    for (int day = 0; day <= SearchDays; ++day) {
        const QDate date = today.addDays(-day);
        if (!isBackupDay(date))
            continue;
        const QDateTime when = backupTime(date);
        if (when.isValid() && when <= now)
            return when;
    }
    return QDateTime();
}

QDateTime Schedule::next(const QDateTime &now) const
{
    if (!isEnabled() || !now.isValid())
        return QDateTime();
    const QDate today = now.toLocalTime().date();
    for (int day = 0; day <= SearchDays; ++day) {
        const QDate date = today.addDays(day);
        if (!isBackupDay(date))
            continue;
        const QDateTime when = backupTime(date);
        if (when.isValid() && when > now)
            return when;
    }
    return QDateTime();
}

bool Schedule::isDue(const QDateTime &now, const QDateTime &since) const
{
    const QDateTime last = previous(now);
    if (!last.isValid())
        return false;
    return !since.isValid() || last > since;
}
