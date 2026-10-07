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

#include "application.h"
#include "mainWin.h"
#include <QFileOpenEvent>
#include <QFileInfo>
#include <QTimer>

namespace Arqiver {

void Application::setInitialWindow(mainWin *window) {
  initialWindow_ = window;
  ready_ = true;
  const QStringList files = pendingFiles_;
  pendingFiles_.clear();
  QTimer::singleShot(0, this, [this, files] {
    for (const QString& file : files)
      openArchive(file);
  });
}

bool Application::event(QEvent *event) {
  if (event->type() == QEvent::FileOpen) {
    const QUrl url = static_cast<QFileOpenEvent*>(event)->url();
    if (url.isLocalFile()) {
      if (ready_)
        openArchive(url.toLocalFile());
      else
        pendingFiles_ << url.toLocalFile();
      event->accept();
      return true;
    }
  }
  return QApplication::event(event);
}

void Application::openArchive(const QString& path) {
  const QFileInfo info(path);
  if (!info.isFile()) return;
  const QString file = info.absoluteFilePath();
  for (QWidget *widget : topLevelWidgets()) {
    if (auto *window = qobject_cast<mainWin*>(widget)) {
      if (window->currentArchive() == file) {
        if (window->isMinimized())
          window->showNormal();
        else
          window->show();
        window->raise();
        window->activateWindow();
        return;
      }
    }
  }
  mainWin *window = initialWindow_;
  initialWindow_.clear();
  if (!window || !window->currentArchive().isEmpty()) {
    window = new mainWin;
    window->setAttribute(Qt::WA_DeleteOnClose);
  }
  window->loadArguments({file});
  window->raise();
  window->activateWindow();
}

}
