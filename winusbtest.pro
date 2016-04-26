QT += core
QT -= gui

CONFIG += c++11

TARGET = winusbtest
CONFIG += console
CONFIG -= app_bundle

TEMPLATE = app

SOURCES += main.cpp

win32: LIBS += -lSetupApi
win32: LIBS += -lOle32
win32: LIBS += -lwinusb

HEADERS += \
    gs_usb.h \
    ch_9.h
