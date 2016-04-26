QT += core
QT -= gui

CONFIG += c++11
QMAKE_CFLAGS += -std=c99

TARGET = winusbtest
CONFIG += console
CONFIG -= app_bundle

TEMPLATE = app

SOURCES += main.cpp \
    gsusb.c

win32: LIBS += -lSetupApi
win32: LIBS += -lOle32
win32: LIBS += -lwinusb

HEADERS += \
    ch_9.h \
    gsusb_def.h \
    gsusb.h
