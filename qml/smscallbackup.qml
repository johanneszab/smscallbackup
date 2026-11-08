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
                         appSettings.contactNames)
    }

    initialPage: Component { MainPage { } }
    cover: Qt.resolvedUrl("cover/CoverPage.qml")
    allowedOrientations: defaultAllowedOrientations

    Connections {
        target: backupTask
        onFinished: {
            if (ok) {
                appSettings.lastBackup = new Date().getTime()
                //: Shown after a backup, %1 and %2 are e.g. "120 messages" and "35 calls"
                app.lastMessage = qsTr("Saved %1 and %2.")
                        .arg(qsTr("%n message(s)", "", messages))
                        .arg(qsTr("%n call(s)", "", calls))
                app.lastMessageIsError = false
            } else {
                app.lastMessage = message
                app.lastMessageIsError = true
            }
        }
    }
}
