# SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
# SPDX-License-Identifier: MIT
# SMS & Call Backup for Sailfish OS
TARGET = smscallbackup

CONFIG += sailfishapp sailfishapp_i18n c++14
QT += concurrent dbus sql
PKGCONFIG += commhistory-qt5

isEmpty(VERSION): VERSION = 0.1.0
DEFINES += APP_VERSION=\\\"$$VERSION\\\"

include(src/core/core.pri)
INCLUDEPATH += src/app

HEADERS += \
    src/app/backupfiles.h \
    src/app/backuptask.h \
    src/app/cli.h \
    src/app/contactnames.h \
    src/app/historystore.h \
    src/app/historywriter.h \
    src/app/restoretask.h \
    src/app/settings.h \
    src/app/simaccounts.h

SOURCES += \
    src/app/backupfiles.cpp \
    src/app/backuptask.cpp \
    src/app/cli.cpp \
    src/app/contactnames.cpp \
    src/app/historystore.cpp \
    src/app/historywriter.cpp \
    src/app/main.cpp \
    src/app/restoretask.cpp \
    src/app/settings.cpp \
    src/app/simaccounts.cpp

DISTFILES += \
    qml/smscallbackup.qml \
    qml/cover/CoverPage.qml \
    qml/pages/AboutPage.qml \
    qml/pages/MainPage.qml \
    qml/pages/RestorePage.qml \
    rpm/smscallbackup.spec \
    smscallbackup.desktop \
    translations/*.ts

SAILFISHAPP_ICONS = 86x86 108x108 128x128 172x172 256x256

TRANSLATIONS += \
    translations/smscallbackup-de.ts \
    translations/smscallbackup-en.ts

license.files = LICENSE
license.path = /usr/share/licenses/$$TARGET
INSTALLS += license
