lessThan(QT_MAJOR_VERSION, 6) {
  error("Arqiver needs at least Qt 6.6.0.")
} else {
  equals(QT_MAJOR_VERSION, 6) {
    lessThan(QT_MINOR_VERSION, 6) {
      error("Arqiver needs at least Qt 6.6.0.")
    }
  } else {
    error("Arqiver cannot be compiled against this version of Qt.")
  }
}

QT += core gui widgets svg dbus

TEMPLATE = app
TARGET = arqiver

HEADERS	+= mainWin.h \
           backends.h \
           label.h \
           treeWidget.h \
           lineedit.h \
           svgicons.h \
           config.h \
           pref.h

SOURCES	+= main.cpp \
           mainWin.cpp \
           treeWidget.cpp \
           backends.cpp \
           svgicons.cpp \
           config.cpp \
           pref.cpp

FORMS += mainWin.ui about.ui pref.ui

RESOURCES += data/arq.qrc

unix {
  #TRANSLATIONS
  exists($$[QT_INSTALL_BINS]/lrelease) {
    TRANSLATIONS = $$system("find data/translations/ -name 'arqiver_*.ts'")
    updateqm.input = TRANSLATIONS
    updateqm.output = data/translations/translations/${QMAKE_FILE_BASE}.qm
    updateqm.commands = $$[QT_INSTALL_BINS]/lrelease ${QMAKE_FILE_IN} -qm data/translations/translations/${QMAKE_FILE_BASE}.qm
    updateqm.CONFIG += no_link target_predeps
    QMAKE_EXTRA_COMPILERS += updateqm
  }

  isEmpty(PREFIX) {
    PREFIX = /usr
  }
  BINDIR = $$PREFIX/bin
  DATADIR = $$PREFIX/share

  DEFINES += DATADIR=\\\"$$DATADIR\\\"

  target.path = $${BINDIR}

  desktop.files = ./data/arqiver.desktop
  desktop.path = $${DATADIR}/applications

  iconsvg.path = $${DATADIR}/icons/hicolor/scalable/apps
  iconsvg.files += data/icons/$${TARGET}.svg

  trans.path = $${DATADIR}/arqiver
  trans.files += data/translations/translations

  INSTALLS += target desktop iconsvg trans
}
