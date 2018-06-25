QT += core gui widgets svg

TEMPLATE = app
TARGET = arqiver

HEADERS	+= mainWin.h \
           tarBackend.h \
           label.h \
           treeWidget.h \
           svgicons.h

SOURCES	+= main.cpp \
           mainWin.cpp \
           tarBackend.cpp \
           svgicons.cpp

FORMS += mainWin.ui

RESOURCES += data/arq.qrc

unix {
  isEmpty(PREFIX) {
    PREFIX = /usr
  }
  BINDIR = $$PREFIX/bin
  DATADIR = $$PREFIX/share

  target.path = $${BINDIR}

  desktop.files = ./data/arqiver.desktop
  desktop.path = $${DATADIR}/applications

  INSTALLS += target desktop
}
