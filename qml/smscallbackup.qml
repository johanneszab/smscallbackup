// SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
// SPDX-License-Identifier: MIT

import QtQuick 2.0
import Sailfish.Silica 1.0
import "pages"

ApplicationWindow {
    id: app

    // Shared with the cover: what happened last, e.g. "Saved 120 messages and 35 calls."
    property string lastMessage
    property bool lastMessageIsError

    function startBackup() {
        lastMessage = ""
        backupTask.start(appSettings.folder, appSettings.backupMessages, appSettings.backupCalls,
                         appSettings.contactNames, appSettings.incremental)
    }

    // What a backup did, from the result of BackupTask.finished.
    function backupText(result) {
        var messages = result.messages || 0
        var calls = result.calls || 0
        if (result.incremental) {
            if (messages === 0 && calls === 0)
                return qsTr("Nothing new since the last backup.")
            //: Shown after a backup that added to sms.xml and calls.xml, %1 and %2 are e.g. "3 messages" and "1 call"
            return qsTr("Added %1 and %2.").arg(qsTr("%n message(s)", "", messages))
                                           .arg(qsTr("%n call(s)", "", calls))
        }
        //: Shown after a backup, %1 and %2 are e.g. "120 messages" and "35 calls"
        return qsTr("Saved %1 and %2.").arg(qsTr("%n message(s)", "", messages))
                                       .arg(qsTr("%n call(s)", "", calls))
    }

    // Minutes after midnight as a time of day in the phone's format, e.g. "03:00".
    function timeText(minutes) {
        return Format.formatDate(new Date(2000, 0, 1, Math.floor(minutes / 60), minutes % 60), Formatter.TimeValue)
    }

    function dateTimeText(msecs) {
        var date = new Date(msecs)
        return Format.formatDate(date, Formatter.DateMedium) + " " + Format.formatDate(date, Formatter.TimeValue)
    }

    // e.g. "Daily at 03:00", "Every Monday at 03:00"
    function scheduleText() {
        var time = timeText(appSettings.scheduleTime)
        switch (appSettings.scheduleFrequency) {
        case 1:
            //: Automatic backup, %1 is a time
            return qsTr("Daily at %1").arg(time)
        case 2:
            //: Automatic backup, %1 is a day of the week, %2 a time
            return qsTr("Every %1 at %2").arg(Qt.locale().dayName(appSettings.scheduleWeekday % 7, Locale.LongFormat))
                                         .arg(time)
        case 3:
            //: Automatic backup, %1 is a day of the month (1-31), %2 a time
            return qsTr("Monthly on day %1 at %2").arg(appSettings.scheduleMonthday).arg(time)
        default:
            //: Automatic backup
            return qsTr("Off")
        }
    }

    initialPage: Component { MainPage { } }
    cover: Qt.resolvedUrl("cover/CoverPage.qml")
    allowedOrientations: defaultAllowedOrientations

    Connections {
        target: backupTask
        onFinished: {
            if (ok) {
                appSettings.lastBackup = new Date().getTime()
                var text = app.backupText(result)
                if ((result.missingParts || 0) > 0)
                    text += " " + qsTr("%n attachment(s) could not be read and are missing in the backup.", "",
                                       result.missingParts)
                app.lastMessage = text
                app.lastMessageIsError = false
            } else {
                app.lastMessage = message
                app.lastMessageIsError = true
            }
        }
    }

    Connections {
        target: Qt.application
        onStateChanged: {
            // The automatic backup may have run in the meantime.
            if (Qt.application.state === Qt.ApplicationActive)
                appSettings.reload()
        }
    }
}
