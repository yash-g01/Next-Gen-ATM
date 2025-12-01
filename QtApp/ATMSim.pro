QT += core gui network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

TARGET = NextGenATM
TEMPLATE = app

DESTDIR = ./app

SOURCES += atm_qt_app.cpp qrcodegen.cpp
RESOURCES += assets.qrc

HEADERS += nfcworker.h qrcodegen.hpp
