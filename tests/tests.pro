# SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
# SPDX-License-Identifier: MIT
# Unit tests of the backup format code. Builds with Qt 5 (Sailfish SDK) and Qt 6 (PC):
#   mkdir build-tests && cd build-tests && qmake6 ../tests/tests.pro && make && ./tst_core
TEMPLATE = app
TARGET = tst_core
QT += testlib
QT -= gui
CONFIG += console testcase c++14
CONFIG -= app_bundle

include(../src/core/core.pri)

SOURCES += tst_core.cpp

DEFINES += TESTDATA_DIR=\\\"$$PWD/data\\\"
