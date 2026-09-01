QT += core gui widgets network sql

CONFIG += c++17
CONFIG -= app_bundle

# Qt Creator 默认使用 shadow build；把源码根目录编译进程序，避免从运行目录猜路径。
DEFINES += RETRO_SOURCE_DIR=\"$$PWD\"

TEMPLATE = app
TARGET = retro_dcs_demo

macx {
    # Homebrew Qt 5 still requests the legacy AGL framework, which was
    # removed from recent macOS SDKs. This demo only needs modern OpenGL.
    QMAKE_LIBS_OPENGL = -framework OpenGL

    # Qt 5's mkspec predates recent macOS SDK version numbers.
    CONFIG += sdk_no_version_check
}

SOURCES += \
    src/main.cpp \
    src/mainwindow.cpp \
    src/retro_style.cpp \
    src/manager_client.cpp \
    src/project_metadata.cpp \
    src/csv_utils.cpp

HEADERS += \
    src/mainwindow.h \
    src/retro_style.h \
    src/manager_client.h \
    src/project_metadata.h \
    src/csv_utils.h
