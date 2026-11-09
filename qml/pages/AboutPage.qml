// SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
// SPDX-License-Identifier: MIT

import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: page

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column

            width: parent.width
            spacing: Theme.paddingLarge

            PageHeader {
                title: qsTr("About")
            }

            Image {
                anchors.horizontalCenter: parent.horizontalCenter
                source: "/usr/share/icons/hicolor/172x172/apps/smscallbackup.png"
                width: Theme.iconSizeExtraLarge
                height: width
                sourceSize.width: width
                sourceSize.height: height
            }

            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                font.pixelSize: Theme.fontSizeLarge
                color: Theme.highlightColor
                text: qsTr("SMS & Call Backup")
            }

            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                color: Theme.secondaryHighlightColor
                text: qsTr("Version %1").arg(appVersion)
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.highlightColor
                text: qsTr("Backs up your SMS, MMS and call log in the XML format of the Android app SMS Backup & Restore, and restores such backups, also their RCS chats. Move your history between Android and Sailfish OS in both directions, or keep a copy of it, by hand or automatically.")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("Restoring only adds what is not on the phone yet, so restoring a backup twice does no harm. Restored messages go into the existing conversations of the Messages app.")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                text: qsTr("Not affiliated with SyncTech, the maker of SMS Backup & Restore. Its file format is documented on synctech.com.au.")
            }

            SectionHeader {
                text: qsTr("Command line")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                font.family: "monospace"
                color: Theme.secondaryHighlightColor
                text: "smscallbackup --backup [FOLDER]\nsmscallbackup --restore FILE… [--dry-run]\nsmscallbackup --scheduled [--force]\nsmscallbackup --help"
            }

            SectionHeader {
                text: qsTr("License")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.highlightColor
                text: qsTr("Copyright © 2026 Johannes Meyer zum Alten Borgloh. Free software under the MIT license.")
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                //: Opens the project page in the browser
                text: qsTr("Source code")
                onClicked: Qt.openUrlExternally("https://github.com/johanneszab/smscallbackup")
            }
        }

        VerticalScrollDecorator { }
    }
}
