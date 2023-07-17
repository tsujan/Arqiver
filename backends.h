/*
 * Copyright (C) Pedram Pourang (aka Tsu Jan) 2018-2021 <tsujan2000@gmail.com>
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
#include <QHash>
#include <QString>
#include <QStringList>
#include <QFileSystemWatcher>

namespace Arqiver {

class Backend : public QObject {
  Q_OBJECT

public:
  Backend(QObject *parent = 0);
  ~Backend();

  QString getMimeType(const QString &fname);
  void loadFile(const QString& path, bool withPassword = false);
  bool canModify(bool *canUpdate) const;

  QString currentFile() const;

  QStringList hierarchy() const;
  QString singleRoot() const;
  QString sizeString(const QString& file) const;
  double size(const QString& file) const;
  double csize(const QString& file) const;
  bool isDir(const QString& file) const;
  bool isLink(const QString& file) const;
  QString linkTo(const QString& file) const;

  void startAdd(const QStringList& paths, const QString& parentPath = QString(), bool absolutePaths = false);
  void startRemove(const QStringList& paths);
  void startExtract(const QString& path, const QString& file = QString(), bool overwrite = true, bool preservePaths = true);
  void startExtract(const QString& path, const QStringList& files, bool overwrite = true, bool preservePaths = true);

  bool startViewFile(const QString& path);
  void extractTempFiles(const QStringList& paths);

  void setTarCommand(const QString& cmnd);

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

  bool hasList() const {
    return !contents_.isEmpty();
  }

  bool hasEncryptedList() const {
    return encryptedList_;
  }
  void encryptFileList() {
    encryptedList_ = true;
  }

  bool isEncryptedPath(const QString& path) const;
  bool isSingleExtracted(const QString& archivePath) const;
  void removeSingleExtracted(const QString& archivePath) const;

  bool isWorking();
  void killProc();

signals:
  void loadingFinished(); // emitted regardless of success
  void loadingSuccessful();
  void extractionFinished(); // emitted regardless of success
  void extractionSuccessful();
  void processStarting();
  void progressUpdate(int, const QString&);
  void processFinished(bool, const QString&);
  void archivingSuccessful();
  void encryptedList(const QString& path);
  void errorMsg(const QString& msg);
  void tempFilesExtracted(const QStringList& files);
  void fileModified(bool modified);

public slots:
  void updateArchive();

private slots:
  void startInsertFromQueue() {
    startAdd(insertQueue_);
  }
  void startList(bool withPassword = false);
  void procFinished(int retcode, QProcess::ExitStatus);
  void processData();
  void onError(QProcess::ProcessError error);

private:
  bool allChildrenExyracted(const QString& parent) const;
  void parseLines(QStringList& lines);

  QString tarCmnd_;

  QProcess proc_;
  QProcess tmpProc_; // for secondary jobs

  QHash<QString, QString> mimeTypes_; // to speed up mime type search

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
  QStringList encryptedPaths_; // for 7z
  bool isGzip_, is7z_;
  QString pswrd_; // for 7z
  QString archiveSingleRoot_;

  QString result_; // the message sent by procFinished()

  QString data_; // used when processing data

  bool isKilled_;

  QFileSystemWatcher *watcher_;
  QStringList changedFiles_;

  bool startBackslash_; // used in a workaround for a bug in bsdtar
};

}

#endif
