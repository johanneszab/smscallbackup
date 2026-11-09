// SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
// SPDX-License-Identifier: MIT

import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: page

    readonly property bool enabledSchedule: appSettings.scheduleFrequency > 0

    function lastResultText() {
        if (!appSettings.lastScheduledOk)
            return appSettings.lastScheduledError
        var messages = appSettings.lastScheduledMessages
        var calls = appSettings.lastScheduledCalls
        if (!appSettings.incremental)
            return qsTr("Saved %1 and %2.").arg(qsTr("%n message(s)", "", messages))
                                           .arg(qsTr("%n call(s)", "", calls))
        if (messages === 0 && calls === 0)
            return qsTr("Nothing new since the last backup.")
        return qsTr("Added %1 and %2.").arg(qsTr("%n message(s)", "", messages))
                                       .arg(qsTr("%n call(s)", "", calls))
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column

            width: parent.width

            PageHeader {
                title: qsTr("Automatic backup")
            }

            ComboBox {
                //: How often the automatic backup runs
                label: qsTr("Back up")
                currentIndex: appSettings.scheduleFrequency
                menu: ContextMenu {
                    MenuItem {
                        //: Automatic backup
                        text: qsTr("Never")
                        onClicked: appSettings.scheduleFrequency = 0
                    }
                    MenuItem {
                        text: qsTr("Every day")
                        onClicked: appSettings.scheduleFrequency = 1
                    }
                    MenuItem {
                        text: qsTr("Every week")
                        onClicked: appSettings.scheduleFrequency = 2
                    }
                    MenuItem {
                        text: qsTr("Every month")
                        onClicked: appSettings.scheduleFrequency = 3
                    }
                }
            }

            ComboBox {
                visible: appSettings.scheduleFrequency === 2
                //: Day of the week of the automatic backup
                label: qsTr("Day")
                currentIndex: appSettings.scheduleWeekday - 1
                menu: ContextMenu {
                    Repeater {
                        model: 7
                        MenuItem {
                            // Monday first; dayName() counts from Sunday = 0
                            text: Qt.locale().dayName((index + 1) % 7, Locale.LongFormat)
                            onClicked: appSettings.scheduleWeekday = index + 1
                        }
                    }
                }
            }

            Slider {
                id: monthdaySlider

                visible: appSettings.scheduleFrequency === 3
                width: parent.width
                //: Day of the month of the automatic backup
                label: qsTr("Day of the month")
                minimumValue: 1
                maximumValue: 31
                stepSize: 1
                value: appSettings.scheduleMonthday
                valueText: Math.round(value)
                onDownChanged: {
                    if (!down)
                        appSettings.scheduleMonthday = Math.round(value)
                }
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: appSettings.scheduleFrequency === 3 && appSettings.scheduleMonthday > 28
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("In months without day %1, on their last day.").arg(appSettings.scheduleMonthday)
            }

            ValueButton {
                visible: page.enabledSchedule
                //: Time of day of the automatic backup
                label: qsTr("Time")
                value: app.timeText(appSettings.scheduleTime)
                onClicked: {
                    var dialog = pageStack.push("Sailfish.Silica.TimePickerDialog", {
                        hour: Math.floor(appSettings.scheduleTime / 60),
                        minute: appSettings.scheduleTime % 60
                    })
                    dialog.accepted.connect(function() {
                        appSettings.scheduleTime = dialog.hour * 60 + dialog.minute
                    })
                }
            }

            Item {
                width: 1
                height: Theme.paddingMedium
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: page.enabledSchedule && appSettings.nextScheduledBackup > 0
                wrapMode: Text.Wrap
                color: Theme.highlightColor
                text: appSettings.nextScheduledBackup <= new Date().getTime()
                      ? qsTr("Next backup: as soon as possible")
                      //: %1 is a date and time
                      : qsTr("Next backup: %1").arg(app.dateTimeText(appSettings.nextScheduledBackup))
            }

            Item {
                width: 1
                height: Theme.paddingMedium
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("The backup runs in the background, also when the app is closed, and uses the choices of the main page: what to back up, the folder and whether to add to the same files. It starts within 15 minutes of the time set, or, if the phone is switched off or asleep then, as soon as it is on again. If it fails, a notification tells why.")
            }

            SectionHeader {
                visible: appSettings.lastScheduledBackup > 0
                text: qsTr("Last automatic backup")
            }

            DetailItem {
                visible: appSettings.lastScheduledBackup > 0
                label: qsTr("When")
                value: appSettings.lastScheduledBackup > 0 ? app.dateTimeText(appSettings.lastScheduledBackup) : ""
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: appSettings.lastScheduledBackup > 0
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: appSettings.lastScheduledOk ? Theme.highlightColor : Theme.errorColor
                text: page.lastResultText()
            }
        }

        VerticalScrollDecorator { }
    }
}
