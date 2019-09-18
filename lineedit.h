/*
 * Copyright (C) Pedram Pourang (aka Tsu Jan) 2019 <tsujan2000@gmail.com>
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

#ifndef LINEEDIT_H
#define LINEEDIT_H

#include <QLineEdit>
#include <QKeyEvent>

namespace Arqiver {

class LineEdit : public QLineEdit {
    Q_OBJECT
public:
  LineEdit (QWidget *parent = nullptr) : QLineEdit(parent) {
    setToolTip(tr("You could type inside the main view.\nClear text with the Escape key."));
    setClearButtonEnabled(true);
  }
  ~LineEdit() {}

  void pressKey (QKeyEvent *event) {
    keyPressEvent (event);
  }

protected:
  virtual void keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
      clear();
      event->accept();
      return;
    }
    QLineEdit::keyPressEvent(event);
  }
};

}

#endif // LINEEDIT_H
