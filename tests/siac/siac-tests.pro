QT += core testlib
QT -= gui
CONFIG += console testcase c++17
TEMPLATE = app
TARGET = tst_sessionswitchplanner

INCLUDEPATH += ../../app

SOURCES += \
    tst_sessionswitchplanner.cpp \
    ../../app/siac/sessionswitchplanner.cpp \
    ../../app/siac/sessiontransitionstate.cpp \
    ../../app/siac/clipboard/clipboardprotocol.cpp

HEADERS += \
    ../../app/siac/sessionswitchplanner.h \
    ../../app/siac/sessiontransitionstate.h \
    ../../app/siac/clipboard/clipboardprotocol.h
