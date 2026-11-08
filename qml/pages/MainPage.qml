// SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
// SPDX-License-Identifier: MIT

import QtQuick 2.0
import Sailfish.Silica 1.0
import Sailfish.Pickers 1.0

Page {
    id: page

    property var backups: []
    property var counts: ({})
    property var pickedFiles: []

    function refresh() {
        backups = backupFiles.list(appSettings.folder)
        counts = backupTask.historyCounts()
    }

    function openRestore(files) {
        pageStack.push(Qt.resolvedUrl("RestorePage.qml"), { files: files })
    }

    function describe(backup) {
        var parts = []
        if (backup.messages >= 0)
            parts.push(qsTr("%n message(s)", "", backup.messages))
        if (backup.calls >= 0)
            parts.push(qsTr("%n call(s)", "", backup.calls))
        parts.push(Format.formatFileSize(backup.size))
        return parts.join(" · ")
    }

    Component.onCompleted: refresh()

    onStatusChanged: {
        if (status !== PageStatus.Active)
            return
        if (pickedFiles.length > 0) {
            // back from the file picker
            var files = pickedFiles
            pickedFiles = []
            openRestore(files)
        } else {
            refresh()
        }
    }

    Connections {
        target: backupTask
        onFinished: page.refresh()
    }

    SilicaListView {
        id: listView

        anchors.fill: parent
        model: page.backups

        PullDownMenu {
            busy: backupTask.running

            MenuItem {
                //: Pull-down menu
                text: qsTr("About")
                onClicked: pageStack.push(Qt.resolvedUrl("AboutPage.qml"))
            }
            MenuItem {
                //: Pull-down menu: pick backup files anywhere on the phone
                text: qsTr("Restore from files")
                onClicked: pageStack.push(filePicker)
            }
            MenuItem {
                text: backupTask.running ? qsTr("Cancel backup") : qsTr("Back up now")
                enabled: backupTask.running || appSettings.backupMessages || appSettings.backupCalls
                onClicked: backupTask.running ? backupTask.cancel() : app.startBackup()
            }
        }

        header: Column {
            width: listView.width

            PageHeader {
                title: qsTr("SMS & Call Backup")
            }

            SectionHeader {
                text: qsTr("Back up")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: page.counts.ok === true
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryHighlightColor
                //: %1 and %2 are e.g. "120 SMS" and "35 calls"
                text: qsTr("On this phone: %1 and %2.")
                      .arg(qsTr("%n SMS", "", page.counts.sms || 0))
                      .arg(qsTr("%n call(s)", "", page.counts.calls || 0))
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: page.counts.ok === false
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.errorColor
                text: page.counts.error || ""
            }

            TextSwitch {
                text: qsTr("Messages")
                //: Description of the "Messages" switch
                description: (page.counts.mms || 0) > 0
                             ? qsTr("SMS. MMS are not included yet.")
                             : qsTr("SMS, saved in sms-<date>.xml")
                automaticCheck: false
                checked: appSettings.backupMessages
                enabled: !backupTask.running
                onClicked: appSettings.backupMessages = !checked
            }

            TextSwitch {
                text: qsTr("Call log")
                description: qsTr("Saved in calls-<date>.xml")
                automaticCheck: false
                checked: appSettings.backupCalls
                enabled: !backupTask.running
                onClicked: appSettings.backupCalls = !checked
            }

            TextSwitch {
                visible: backupTask.contactNamesAvailable
                text: qsTr("Contact names")
                description: qsTr("Adds the contact's name to each entry, as the Android app does. Only for reading the file; restoring does not need them.")
                automaticCheck: false
                checked: appSettings.contactNames
                enabled: !backupTask.running
                onClicked: appSettings.contactNames = !checked
            }

            ValueButton {
                //: The folder backups are saved in
                label: qsTr("Folder")
                value: backupFiles.displayPath(appSettings.folder)
                enabled: !backupTask.running
                onClicked: pageStack.push(folderPicker)
            }

            Item {
                width: 1
                height: Theme.paddingLarge
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: backupTask.running ? qsTr("Cancel") : qsTr("Back up now")
                enabled: backupTask.running || appSettings.backupMessages || appSettings.backupCalls
                onClicked: backupTask.running ? backupTask.cancel() : app.startBackup()
            }

            Item {
                width: 1
                height: Theme.paddingMedium
            }

            ProgressBar {
                width: parent.width
                visible: backupTask.running
                minimumValue: 0
                maximumValue: 1
                value: backupTask.progress
                label: backupTask.status
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: !backupTask.running && text.length > 0
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize: Theme.fontSizeSmall
                color: app.lastMessageIsError ? Theme.errorColor : Theme.highlightColor
                text: app.lastMessage.length > 0
                      ? app.lastMessage
                      : appSettings.lastBackup > 0
                        ? qsTr("Last backup: %1").arg(Format.formatDate(new Date(appSettings.lastBackup),
                                                                        Formatter.TimepointRelative))
                        : ""
            }

            SectionHeader {
                text: qsTr("Restore")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryHighlightColor
                text: page.backups.length > 0
                      ? qsTr("Tap a backup to see what it contains. Only what is not on the phone yet is restored.")
                      : qsTr("No backups in this folder yet. Backups of SMS Backup & Restore on Android work too: copy their sms-….xml and calls-….xml files into this folder, or choose them with “Restore from files” in the pull-down menu.")
            }

            Item {
                width: 1
                height: Theme.paddingMedium
            }
        }

        delegate: ListItem {
            id: item

            contentHeight: column.height + 2 * Theme.paddingMedium
            menu: Component {
                ContextMenu {
                    MenuItem {
                        text: qsTr("Restore")
                        onClicked: page.openRestore(modelData.files)
                    }
                    MenuItem {
                        text: qsTr("Delete")
                        onClicked: {
                            var files = modelData.files
                            //: Remorse timer before deleting a backup
                            item.remorseAction(qsTr("Deleting"), function() {
                                backupFiles.remove(files)
                                page.refresh()
                            })
                        }
                    }
                }
            }
            onClicked: page.openRestore(modelData.files)

            Column {
                id: column

                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                anchors.verticalCenter: parent.verticalCenter

                Label {
                    width: parent.width
                    truncationMode: TruncationMode.Fade
                    color: item.highlighted ? Theme.highlightColor : Theme.primaryColor
                    text: Format.formatDate(new Date(modelData.date), Formatter.DateMedium) + "  "
                          + Format.formatDate(new Date(modelData.date), Formatter.TimeValue)
                }
                Label {
                    width: parent.width
                    truncationMode: TruncationMode.Fade
                    font.pixelSize: Theme.fontSizeSmall
                    color: item.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                    text: page.describe(modelData)
                }
                Label {
                    width: parent.width
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: item.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                    text: modelData.names.join(", ")
                }
            }
        }

        VerticalScrollDecorator { }
    }

    Component {
        id: folderPicker

        FolderPickerDialog {
            title: qsTr("Backup folder")
            path: StandardPaths.documents
            onAccepted: appSettings.folder = selectedPath
        }
    }

    Component {
        id: filePicker

        MultiFilePickerDialog {
            //: File picker title
            title: qsTr("Backup files")
            nameFilters: [ "*.xml", "*.zip" ]
            onAccepted: {
                var files = []
                for (var i = 0; i < selectedContent.count; ++i)
                    files.push(selectedContent.get(i).filePath)
                page.pickedFiles = files
            }
        }
    }
}
