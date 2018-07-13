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

#ifndef PREF_H
#define PREF_H

#include <QDialog>
#include <QCloseEvent>

namespace Arqiver {

namespace Ui {
class PrefDialog;
}

class PrefDialog : public QDialog {
  Q_OBJECT

public:
  explicit PrefDialog(QWidget *parent = nullptr);
  ~PrefDialog();

private slots:
  void prefStartSize(int value);
  void prefIconSize(int index);

private:
  void closeEvent(QCloseEvent *event);
  void showPrompt(const QString& str = QString());

  Ui::PrefDialog *ui;
  QWidget *parent_;
  int initialIconSize_;
};

}

#endif // PREF_H
