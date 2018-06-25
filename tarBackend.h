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

#ifndef ARQIVER_TAR_BACKEND_H
#define ARQIVER_TAR_BACKEND_H

#include <QProcess>
#include <QString>
#include <QStringList>

namespace Arqiver {

class Backend : public QObject{
  Q_OBJECT
public:
  Backend(QObject *parent = 0);
  ~Backend();

  void loadFile(const QString& path);
  bool canModify(); //run on the current file

  //Listing routines
  QString currentFile();
  bool isWorking(); //is this currently still making changes?

  //Contents listing
  QStringList heirarchy(); //returns all the file paths within the archive
  double size(QString file);
  double csize(QString file);
  bool isDir(QString file);
  bool isLink(QString file);
  QString linkTo(QString file);

  //Modification routines
  void startAdd(QStringList paths, bool absolutePaths = false);
  void startRemove(QStringList paths);
  void startExtract(QString path, bool overwrite, QString file="");
  void startExtract(QString path, bool overwrite, QStringList files);

  void startViewFile(QString path);
  QString extractFile(QString path);

signals:
  void FileLoaded();
  void ExtractSuccessful();
  void ProcessStarting();
  void ProgressUpdate(int, QString); //percentage, text
  void ProcessFinished(bool, QString); //success, text
  void ArchivalSuccessful();

private slots:
  void startInsertFromQueue() {
    startAdd(insertQueue_);
  }
  void startList();
  void procFinished(int retcode, QProcess::ExitStatus);
  void processData();

private:
  void parseLines(QStringList lines);

  QProcess PROC;

  QString filepath_, tmpfilepath_, arqiverDir_;
  QStringList flags_;
  QHash<QString, QStringList> contents_; //<filepath, [attributes, size, compressed size]

  QStringList insertQueue_;

  bool LIST;
  QString archiveParentDir_;
};

}

#endif
