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

#ifndef BACKENDS_H
#define BACKENDS_H

#include <QProcess>
#include <QString>
#include <QStringList>

namespace Arqiver {

class Backend : public QObject{
  Q_OBJECT
public:
  Backend(QObject *parent = 0);
  ~Backend();

  QString getMimeType(const QString &fname);
  void loadFile(const QString& path, bool withPassword = false);
  bool canModify();

  QString currentFile();
  bool isWorking();

  QStringList heirarchy();
  double size(QString file);
  double csize(QString file);
  bool isDir(QString file);
  bool isLink(QString file);
  QString linkTo(QString file);

  void startAdd(QStringList paths, bool absolutePaths = false);
  void startRemove(QStringList paths);
  void startExtract(QString path, QString file="", bool overwrite = true, bool preservePaths = true);
  void startExtract(QString path, QStringList files, bool overwrite = true, bool preservePaths = true);

  void startViewFile(QString path);
  QString extractFile(QString path);

  bool isGzip() const {
    return isGzip_;
  }
  bool is7z() const {
    return is7z_;
  }
  bool isEncrypted() const {
    return is7z_ && encryptionQueried_ && encrypted_;
  }
  QString getPswrd() const {
    return pswrd_;
  }
  void setPswrd(const QString& pswrd) {
    pswrd_ = pswrd;
  }

  void encryptFileList() {
    encryptedList_ = true;
  }

signals:
  void FileLoaded();
  void ExtractSuccessful();
  void ProcessStarting();
  void ProgressUpdate(int, QString);
  void ProcessFinished(bool, QString);
  void ArchivalSuccessful();
  void encryptedList(const QString& path);

private slots:
  void startInsertFromQueue() {
    startAdd(insertQueue_);
  }
  void startList(bool withPassword = false);
  void procFinished(int retcode, QProcess::ExitStatus);
  void processData();

private:
  void parseLines(QStringList lines);

  QProcess PROC;

  QString filepath_, tmpfilepath_, arqiverDir_;
  QStringList fileArgs_;
  /* "keyArgs_" is used, instead of QProcess::arguments(), to exlude file/item names
     and to know which signal(s) should be emitted when the process is finished. The
     reason is that file names can be the same as arguments. */
  QStringList keyArgs_;
  QHash<QString, QStringList> contents_; // {filepath, (attributes, size, compressed size)}

  QStringList insertQueue_;

  bool LIST;
  bool starting7z_; // for 7z
  bool encryptionQueried_, encrypted_, encryptedList_; // for 7z
  bool isGzip_, is7z_;
  QString pswrd_; // for 7z
  QString archiveParentDir_;
};

}

#endif
