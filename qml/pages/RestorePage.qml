// SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
// SPDX-License-Identifier: MIT

import QtQuick 2.0
import Sailfish.Silica 1.0
import SmsCallBackup 1.0

Page {
    id: page

    property var files: []

    readonly property int taskState: restoreTask.state
    readonly property var summary: restoreTask.summary
    readonly property var result: restoreTask.result
    readonly property bool busy: taskState === RestoreTask.Analyzing || taskState === RestoreTask.Restoring
    readonly property bool analyzed: taskState === RestoreTask.Ready || taskState === RestoreTask.Restoring
                                     || taskState === RestoreTask.Finished
                                     || (taskState === RestoreTask.Failed && summary.files !== undefined)
    readonly property int newEntries: (summary.newMessages || 0) + (summary.newCalls || 0)
    property int accountIndex: 0

    function fileName(path) {
        return path.substring(path.lastIndexOf("/") + 1)
    }

    function formatDate(msecs) {
        return msecs > 0 ? Format.formatDate(new Date(msecs), Formatter.DateMedium) : ""
    }

    function notes() {
        var lines = []
        if ((summary.missingAttachments || 0) > 0)
            lines.push(qsTr("%n attachment(s) of MMS are not in the backup (it was made without them). Their messages are restored without them.",
                            "", summary.missingAttachments))
        var skipped = summary.skipped || []
        for (var i = 0; i < skipped.length; ++i)
            //: e.g. "Left out: 3 drafts"
            lines.push(qsTr("Left out: %1 %2").arg(skipped[i].count).arg(skipped[i].reason))
        return lines.join("\n")
    }

    function resultText() {
        var messages = result.messages || 0
        var calls = result.calls || 0
        //: %1 and %2 are e.g. "120 messages" and "35 calls"
        return qsTr("Restored %1 and %2.").arg(qsTr("%n message(s)", "", messages))
                                          .arg(qsTr("%n call(s)", "", calls))
    }

    backNavigation: taskState !== RestoreTask.Restoring

    Component.onCompleted: restoreTask.analyze(files)
    Component.onDestruction: {
        if (restoreTask.state === RestoreTask.Analyzing)
            restoreTask.cancel()
        else if (restoreTask.state !== RestoreTask.Restoring)
            restoreTask.reset()
    }

    RemorsePopup {
        id: remorse
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column

            width: parent.width

            PageHeader {
                title: qsTr("Restore")
                description: page.files.map(page.fileName).join(", ")
            }

            ProgressBar {
                width: parent.width
                visible: page.busy
                minimumValue: 0
                maximumValue: 1
                value: restoreTask.progress
                label: restoreTask.status
            }

            Column {
                width: parent.width
                visible: page.analyzed

                DetailItem {
                    visible: (summary.messages || 0) > 0
                    label: qsTr("Messages in the backup")
                    value: summary.messages || 0
                }
                DetailItem {
                    visible: (summary.mms || 0) > 0
                    //: Number of MMS among the messages of the backup
                    label: qsTr("MMS among them")
                    value: summary.mms || 0
                }
                DetailItem {
                    visible: (summary.messages || 0) > 0
                    //: Number of messages of the backup that are not on the phone
                    label: qsTr("New for this phone")
                    value: summary.newMessages || 0
                }
                DetailItem {
                    visible: (summary.calls || 0) > 0
                    label: qsTr("Calls in the backup")
                    value: summary.calls || 0
                }
                DetailItem {
                    visible: (summary.calls || 0) > 0
                    label: qsTr("New for this phone")
                    value: summary.newCalls || 0
                }
                DetailItem {
                    visible: page.newEntries > 0
                    //: Date range of the new entries, followed by "to"
                    label: qsTr("From")
                    value: page.formatDate(summary.firstTime || 0)
                }
                DetailItem {
                    visible: page.newEntries > 0
                    label: qsTr("To")
                    value: page.formatDate(summary.lastTime || 0)
                }
            }

            Item {
                width: 1
                height: Theme.paddingMedium
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: page.analyzed && text.length > 0
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryHighlightColor
                text: page.notes()
            }

            Item {
                width: 1
                height: Theme.paddingLarge
            }

            ComboBox {
                id: accountBox

                visible: page.taskState === RestoreTask.Ready && page.newEntries > 0
                         && restoreTask.accounts.length > 1
                //: The SIM card the restored history is attached to
                label: qsTr("SIM")
                description: qsTr("The messages and calls are filed under this SIM.")
                currentIndex: page.accountIndex
                menu: ContextMenu {
                    Repeater {
                        model: restoreTask.accounts
                        MenuItem {
                            text: modelData.name
                        }
                    }
                }
                onCurrentIndexChanged: page.accountIndex = currentIndex
            }

            TextSwitch {
                visible: page.taskState === RestoreTask.Ready && (summary.newMessages || 0) > 0
                text: qsTr("Mark messages as read")
                description: qsTr("Otherwise messages that were unread in the backup show up as new.")
                automaticCheck: false
                checked: appSettings.markAsRead
                onClicked: appSettings.markAsRead = !checked
            }

            Item {
                width: 1
                height: Theme.paddingMedium
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: page.taskState === RestoreTask.Ready && page.newEntries > 0
                text: qsTr("Restore")
                onClicked: {
                    var account = restoreTask.accounts[page.accountIndex].localUid
                    var markAsRead = appSettings.markAsRead
                    //: Remorse timer before restoring
                    remorse.execute(qsTr("Restoring"), function() {
                        restoreTask.restore(account, markAsRead)
                    })
                }
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: page.busy
                text: qsTr("Cancel")
                onClicked: restoreTask.cancel()
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: text.length > 0
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                color: page.taskState === RestoreTask.Failed ? Theme.errorColor : Theme.highlightColor
                text: {
                    switch (page.taskState) {
                    case RestoreTask.Ready:
                        return page.newEntries === 0
                                ? qsTr("Everything in this backup is already on the phone.") : ""
                    case RestoreTask.Restoring:
                        return qsTr("Please keep the app open until the restore is finished.")
                    case RestoreTask.Finished:
                        return page.resultText() + "\n"
                                + qsTr("They are in the Messages and Phone apps now.")
                    case RestoreTask.Failed:
                        var done = (result.messages || 0) + (result.calls || 0)
                        return restoreTask.error + (done > 0 ? "\n" + page.resultText() : "")
                    default:
                        return ""
                    }
                }
            }
        }

        VerticalScrollDecorator { }
    }
}
