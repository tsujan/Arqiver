/*
 * Copyright (C) Pedram Pourang (aka Tsu Jan) 2018-2026 <tsujan2000@gmail.com>
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

#ifndef APPLICATION_H
#define APPLICATION_H

#include <QApplication>
#include <QPointer>

namespace Arqiver {

class mainWin;

class Application : public QApplication {
public:
  Application(int& argc, char **argv) : QApplication(argc, argv) {}
  void setInitialWindow(mainWin *window);

protected:
  bool event(QEvent *event) override;

private:
  void openArchive(const QString& path);
  QPointer<mainWin> initialWindow_;
  QStringList pendingFiles_;
  bool ready_ = false;
};

}

#endif
