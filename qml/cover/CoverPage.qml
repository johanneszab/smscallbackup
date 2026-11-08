// SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
// SPDX-License-Identifier: MIT

import QtQuick 2.0
import Sailfish.Silica 1.0
import SmsCallBackup 1.0

CoverBackground {
    readonly property bool restoring: restoreTask.state === RestoreTask.Restoring

    Column {
        anchors.centerIn: parent
        width: parent.width - 2 * Theme.paddingLarge
        spacing: Theme.paddingMedium

        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            color: Theme.highlightColor
            text: qsTr("SMS & Call Backup")
        }

        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.secondaryHighlightColor
            text: {
                if (backupTask.running)
                    //: Cover, e.g. "Backing up 40%"
                    return qsTr("Backing up %1%").arg(Math.round(backupTask.progress * 100))
                if (restoring)
                    return qsTr("Restoring %1%").arg(Math.round(restoreTask.progress * 100))
                if (app.lastMessage.length > 0)
                    return app.lastMessage
                if (appSettings.lastBackup > 0)
                    return qsTr("Last backup: %1").arg(Format.formatDate(new Date(appSettings.lastBackup),
                                                                         Formatter.TimepointRelative))
                return ""
            }
        }
    }

    CoverActionList {
        enabled: !backupTask.running && !restoring
                 && (appSettings.backupMessages || appSettings.backupCalls)

        CoverAction {
            iconSource: "image://theme/icon-cover-sync"
            onTriggered: app.startBackup()
        }
    }
}
