TEMPLATE = app
CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt

INCLUDEPATH+=$$(SYSROOT)/usr/include/drm
SOURCES += main.c

LIBS+=-ldrm -lgbm -L/usr/lib/opengl/nvidia/lib -lEGL -lGL
