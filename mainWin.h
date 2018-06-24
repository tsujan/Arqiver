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

#ifndef ARQIVER_MAIN_UI_H
#define ARQIVER_MAIN_UI_H

#include <QMainWindow>
#include <QString>
#include <QTreeWidgetItem>
#include <QDropEvent>

#include "tarBackend.h"

namespace Arqiver {

namespace Ui {
	class mainWin;
};

class mainWin : public QMainWindow {
	Q_OBJECT
public:
  mainWin();
  ~mainWin();

  void LoadArguments(QStringList);

private slots:
  void NewArchive();
  void OpenArchive();
  void addFiles();
  void addDirs();
  void remFiles();
  void extractFiles();
  void autoextractFiles();
  void autoArchiveFiles();
  void simpleExtractFiles();
  void extractSelection();
  void ViewFile(QTreeWidgetItem *it);
  void extractFile(QTreeWidgetItem *it);
  void UpdateTree();

  void ProcStarting();
  void ProcFinished(bool, QString);
  void ProcUpdate(int percent, QString txt);

  void selectionChanged();

private:

  QTreeWidgetItem* findItem(QString path, QTreeWidgetItem *start = 0);
  bool cleanTree(QStringList list); // returns true if anything gets cleaned

  QString CreateFileTypes();
  QString OpenFileTypes();
  void dragEnterEvent(QDragEnterEvent *event);
  void dropEvent(QDropEvent *event);

  Ui::mainWin *ui;
  Backend *BACKEND;
  QStringList aaFileList_;
  QString sxPath_, sxFile_;
  QString lastPath_;
  bool expandAll_;
  bool close_;
};

#endif

}
