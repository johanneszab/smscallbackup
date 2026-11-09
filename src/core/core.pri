# SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
# SPDX-License-Identifier: MIT
# Backup format and mapping code. Depends on Qt Core and zlib only, so it is also
# built and tested on a PC (tests/tests.pro).
INCLUDEPATH += $$PWD

HEADERS += \
    $$PWD/historyevent.h \
    $$PWD/mapping.h \
    $$PWD/phonenumber.h \
    $$PWD/schedule.h \
    $$PWD/sbrreader.h \
    $$PWD/sbrrecords.h \
    $$PWD/sbrwriter.h \
    $$PWD/xmlrepairdevice.h \
    $$PWD/zipentrydevice.h

SOURCES += \
    $$PWD/mapping.cpp \
    $$PWD/phonenumber.cpp \
    $$PWD/schedule.cpp \
    $$PWD/sbrreader.cpp \
    $$PWD/sbrrecords.cpp \
    $$PWD/sbrwriter.cpp \
    $$PWD/xmlrepairdevice.cpp \
    $$PWD/zipentrydevice.cpp

LIBS += -lz
