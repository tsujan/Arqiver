/* Adapted from:
 * Lumina Archiver belonging to Lumina Desktop
 * Copyright (c) 2012-2017, Ken Moore (moorekou@gmail.com)
 * License: 3-clause BSD license
 * Homepage: https://github.com/lumina-desktop/lumina
 */

/*
 * Copyright (C) Pedram Pourang (aka Tsu Jan) 2018 <tsujan2000@gmail.com>
 *
 * Arqiver is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Arqiver is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * @license GPL-3.0+ <https://spdx.org/licenses/GPL-3.0+.html>
 */

#include <QApplication>
#include <QFileInfo>

#include "mainWin.h"

int  main(int argc, char *argv[]) {
  QApplication a(argc, argv);
  a.setAttribute(Qt::AA_UseHighDpiPixmaps);

  QStringList args;
  for (int i = 1; i < argc; i++) {
    if (QString(argv[i]).startsWith("--"))
      args << QString(argv[i]);
    else {
      args << QFileInfo (QString(argv[i])).absoluteFilePath();
    }
  }

  Arqiver::mainWin W;
  W.LoadArguments(args);
  W.show();
  return  a.exec();
}
