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

#include "backends.h"
#include <QFile>
#include <QDir>
#include <QDateTime>
#include <QCoreApplication>
#include <QTimer>
#include <QStandardPaths>
#include <QMimeDatabase>
#include <QMimeData>
#include <QRegularExpression>

#ifdef Q_OS_LINUX
#define TAR_CMD "bsdtar"
#else
#define TAR_CMD "tar"
#endif

namespace Arqiver {

Backend::Backend(QObject *parent) : QObject(parent) {
  PROC.setProcessChannelMode(QProcess::MergedChannels);
  connect(&PROC, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &Backend::procFinished);
  connect(&PROC, &QProcess::readyReadStandardOutput, this, &Backend::processData);
  connect(&PROC, &QProcess::started, this, &Backend::processStarting);
  connect(&PROC, &QProcess::errorOccurred, this, &Backend::onError);
  LIST = false;
  isGzip_ = is7z_ = false;
  starting7z_ = encryptionQueried_ = encrypted_ = encryptedList_ = false;
}

Backend::~Backend() {
  if(!arqiverDir_.isEmpty())
    QDir(arqiverDir_).removeRecursively();
}

QString Backend::getMimeType(const QString &fname) {
  QMimeDatabase mimeDatabase;
  QMimeType mimeType = mimeDatabase.mimeTypeForFile(QFileInfo(fname));
  return mimeType.name();
}

void Backend::loadFile(const QString& path, bool withPassword) {
  /* check if the file extraction directory can be made
     but don't create it until a file is viewed */
  const QString curTime = QDateTime::currentDateTime().toString("yyyyMMddhhmmss");
  if (!arqiverDir_.isEmpty())
    QDir(arqiverDir_).removeRecursively();
  QString cache = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
  if (!cache.isEmpty()) {
    QDir cacheDir(cache);
    if (cacheDir.exists()) {
      arqiverDir_ = cache + "/" + "arqiver-" + curTime;
    }
  }

  /*
     NOTE: So far, bsdtar, gzip and 7z are supported.
           Also, password protected 7z archives can be
           extracted and created.

           7z has two kinds of prompts that we avoid:
           password and overwrite prompts.
  */

  filepath_ = path;

  starting7z_ = false;
  if (!withPassword) {
    pswrd_.clear();
    encryptedPaths_.clear();
    encrypted_ = encryptedList_ = encryptionQueried_ = false;
  }

  QString mt = getMimeType(path);
  if (mt == "application/gzip" || mt == "application/x-gzpdf" || mt == "image/svg+xml-compressed") {
    isGzip_ = true; is7z_ = false;
  }
  else if (mt == "application/x-7z-compressed") {
    is7z_ = true; isGzip_ = false;
  }
  else {
    isGzip_ = is7z_ = false;
  }
  tmpfilepath_ = filepath_.section("/", 0, -2) + "/" + ".tmp_arqiver-" + curTime + filepath_.section("/", -1);
  fileArgs_.clear();
  if (is7z_) {
    /*if (path.section("/",-1).section(".",1,-1).split(".").contains("tar"))
      fileArgs_ << "-ttar";*/
    fileArgs_ << filepath_;
  }
  else
    fileArgs_ << "-f" << filepath_; // add the actual archive path
  if(QFile::exists(path))
    startList(withPassword);
  else {
    if (is7z_)
      encryptionQueried_ = true; // an empty archive doesn't have encryption (yet)
    contents_.clear();
    emit loadingFinished();
    emit loadingSuccessful();
    emit processFinished(true, QString());
  }
}

bool Backend::canModify() {
  static QStringList validEXT;
  if (validEXT.isEmpty()) {
    validEXT << ".zip" << ".tar.gz" << ".pdf.gz" << ".svgz" << ".tgz" << ".tar.xz" << ".txz" << ".tar.bz" << ".tbz" << ".tar.bz2" << ".tbz2" << ".tar" << ".tar.lzma" << ".tlz" << ".cpio" << /*".pax" <<*/ ".ar" << /*".shar" <<*/ ".gz" << ".7z";
  }
  for (int i = 0; i < validEXT.length(); i++) {
    if (filepath_.endsWith(validEXT[i]))
      return true;
  }
  return false;
}

QString Backend::currentFile() {
  return filepath_;
}

bool Backend::isWorking(){
  return (PROC.state() != QProcess::Running);
}

//Listing routines
QStringList Backend::hierarchy() {
  return contents_.keys();
}

QString Backend::sizeString(const QString& file) {
  if (contents_.contains(file))
    return contents_.value(file)[1];
  return QString();
}

double Backend::size(const QString& file) {
  if (!contents_.contains(file))
    return -1;
  return contents_.value(file)[1].toDouble();
}

double Backend::csize(const QString& file) {
  if (!contents_.contains(file))
    return -1;
  if (is7z_)
    return contents_.value(file)[2].toDouble();
  return contents_.value(file)[1].toDouble();
}

bool Backend::isDir(const QString& file) {
  if (!contents_.contains(file))
    return false;
  if (is7z_)
    return contents_.value(file)[0].startsWith("D");
  return contents_.value(file)[0].startsWith("d");
}

bool Backend::isLink(const QString& file) {
  if (!contents_.contains(file))
    return false;
  return contents_.value(file)[0].startsWith("l");
}

QString Backend::linkTo(const QString& file) {
  if (!contents_.contains(file))
    return "";
  return contents_.value(file)[2];
}

static inline void skipExistingFiles(QString& file) {
  QString suffix;
  int i = 0;
  while (QFile::exists(file + suffix)) {
    suffix = "-" + QString::number(i);
    i++;
  }
  file += suffix;
}

void Backend::startAdd(QStringList& paths,  bool absolutePaths) {
  keyArgs_.clear();
  /* exclude the archive itself */
  if (paths.contains(filepath_))
    paths.removeAll(filepath_);
  /* no path should be a parent folder of the archive */
  QString parentDir = filepath_.section("/", 0, -2);
  for (int i = 0; !paths.isEmpty() && i < paths.length(); i++) {
    if (parentDir.startsWith (paths[i])) {
      paths.removeAt(i);
      i--;
    }
  }

  if(paths.isEmpty()) return;
  /* no path should be repeated */
  paths.removeDuplicates();

  if (isGzip_) {
    if (QFile::exists(filepath_)) return;
    emit processStarting();
    QProcess tmpProc;
    tmpProc.setStandardOutputFile(filepath_);
    tmpProc.start("gzip", QStringList() << "--to-stdout" << paths[0]); // "gzip -c file > archive.gz"
    while (!tmpProc.waitForFinished(500))
      QCoreApplication::processEvents();
    emit processFinished(tmpProc.exitCode() == 0, "");
    loadFile(filepath_);
    return; // FIXME: Unfortunately, onError() can't be called here
  }
  if (is7z_) {
    QStringList args;
    if (encryptedList_) {
      /* with an encrypted header, password should be given
         (but the operation will fail silently if it's incorrect) */
      if (pswrd_.isEmpty())
        return;
      args << "-mhe=on" << "-p" + pswrd_;
    }
    else if (!pswrd_.isEmpty()) {
      /* always add files with encryption if any */
      args << "-p" + pswrd_;
      encrypted_ = true;
    }
    args << "a" << fileArgs_ << paths;
    starting7z_ = true;
    keyArgs_ << "a";
    PROC.start ("7z", args);
    return;
  }
  /* NOTE: All paths should have the same parent directory.
           Check that and put the wrong paths into insertQueue_. */
  QString parent = paths[0].section("/", 0, -2);
  insertQueue_.clear();
  for (int i = 1; i < paths.length(); i++) {
    if (paths[i].section("/", 0, -2) != parent) {
      insertQueue_ << paths.takeAt(i);
      i--;
    }
  }
  QStringList args;
  args << "-c" << "-a";
  args << fileArgs_;
  //Now setup the parent dir
  if (!absolutePaths) {
    for (int i = 0; i < paths.length(); i++) {
      paths[i] = paths[i].section(parent, 1, -1);
      if (paths[i].startsWith("/"))
        paths[i].remove(0, 1);
    }
    args << "-C" << parent;
  }
  else
    args << "-C" << "/";
  args << paths;
  if (QFile::exists(filepath_)) { // append to the existing archive
    skipExistingFiles(tmpfilepath_); // practically not required
    args.replaceInStrings(filepath_, tmpfilepath_);
    args << "@" + filepath_;
  }
  keyArgs_ << "-c" << "-a" << "-C";
  PROC.start(TAR_CMD, args);
}

void Backend::startRemove(QStringList& paths) {
  keyArgs_.clear();
  if (isGzip_) return;
  if (paths.contains(filepath_))
    paths.removeAll(filepath_);
  if (contents_.isEmpty() || paths.isEmpty() || !QFile::exists(filepath_))
    return; //invalid
  paths.removeDuplicates();
  QStringList args;
  if (is7z_) {
    if (encryptedList_)
      args << "-p" + pswrd_; // we had the password to open the archive
    args << "d" << fileArgs_ << paths;
    starting7z_ = true;
    keyArgs_ << "d";
    PROC.start("7z", args);
    return;
  }
  args << "-c" << "-a";
  args << fileArgs_;
  skipExistingFiles(tmpfilepath_); // practically not required
  args.replaceInStrings(filepath_, tmpfilepath_);
  // Add the include rules for all the files we want to keep (no exclude option in "tar")
  for (int i = 0; i < paths.length(); i++) {
    args << "--exclude" << paths[i];
  }
  args << "@" + filepath_;
  keyArgs_ << "-c" << "-a" << "--exclude";
  PROC.start(TAR_CMD, args);
}

void Backend::startExtract(const QString& path, const QString& file, bool overwrite, bool preservePaths) {
  startExtract(path, QStringList() << file, overwrite, preservePaths);
}

void Backend::startExtract(const QString& path, const QStringList& files, bool overwrite, bool preservePaths) {
  keyArgs_.clear();
  if (isGzip_) {
    /* if the extraction takes place in the same directory, we could do it
       in the usual way but the standard output method works in all cases */
    /*if (0 && path == filepath_.section("/", 0, -2)) {
      PROC.start("gzip", QStringList() << "-d" << "-k" << filepath_);
      return;
    }*/
    emit processStarting();
    QProcess tmpProc;
    QString extName = filepath_.section("/", -1);
    if(extName.contains(".")) {
      extName = extName.section(".", 0, -2);
      if (extName.isEmpty())
        extName = "arqiver-" + QDateTime::currentDateTime().toString("yyyyMMddhhmmss");
      else if (filepath_.endsWith(".svgz") && !extName.endsWith(".svg"))
        extName += ".svg";
    }
    extName = path + "/" + extName;
    skipExistingFiles(extName);
    tmpProc.setStandardOutputFile(extName);
    tmpProc.start("gzip", QStringList() << "-d" << "--to-stdout" << filepath_); // gzip -d -c archive.gz > file
    while (!tmpProc.waitForFinished(500))
      QCoreApplication::processEvents();
    emit processFinished(tmpProc.exitCode() == 0, "");
    emit extractionFinished();
    emit extractionSuccessful();
    return;
  }
  QStringList args;
  if(is7z_) { // extract the whole archive; no selective extraction
    if (encrypted_)
      args << "-p" + pswrd_;
    args << (preservePaths ? "x" : "e") << fileArgs_;
    keyArgs_ << "x" << "e";
    starting7z_ = true;
  }
  else {
    args << "-x" << "--no-same-owner";
    if (!overwrite) // NOTE: We never overwrite in Arqiver. This might be changed later.
      args << "-k";
    args << fileArgs_;
    for (int i = 0; i < files.length(); i++) {
      if (files[i].simplified().isEmpty())
        continue;
      args << "--include" << files[i] << "--strip-components" << QString::number(files[i].count("/"));
    }
    keyArgs_ << "-x";
  }
  QString xPath = path;
  if (!archiveParentDir_.isEmpty() && archiveParentDir_.startsWith("."))
    archiveParentDir_.remove(0, 1); // no hidden extraction folder (with rpm)
  if (!archiveParentDir_.isEmpty()) {
    if(QFile::exists(xPath + "/" + archiveParentDir_)) {
      QDir dir (xPath);
      QString subdirName = archiveParentDir_ + "-arqiver-" + QDateTime::currentDateTime().toString("yyyyMMddhhmmss");
      xPath += "/" + subdirName;

      /* this is practically impossible */
      int i = 0;
      QString suffix;
      while (QFile::exists(xPath + suffix)) {
        suffix = QString::number(i);
        ++i;
      }
      xPath += suffix;
      subdirName += suffix;

      dir.mkdir(subdirName);
    }
  }
  else {
    QDir dir (xPath);
    QString subdirName = filepath_.section("/", -1) + "-arqiver-" + QDateTime::currentDateTime().toString("yyyyMMddhhmmss");
    xPath += "/" + subdirName;

    int i = 0;
    QString suffix;
    while (QFile::exists(xPath + suffix)) {
      suffix = QString::number(i);
      ++i;
    }
    xPath += suffix;
    subdirName += suffix;

    dir.mkdir(subdirName);
  }
  if(is7z_) {
    args << "-o" + xPath;
    PROC.start("7z", args);
  }
  else {
    args << "-C" << xPath;
    keyArgs_ << "-C";
    PROC.start(TAR_CMD, args); // doesn't create xPath if not existing
  }
}

bool Backend::isEncryptedPath(const QString& path) const {
  if (!is7z_) return false;
  return (encryptedList_ || encryptedPaths_.contains(path));
}

bool Backend::is7zSingleExtracted(const QString& archivePath) const {
  return (!arqiverDir_.isEmpty() && QFile::exists(arqiverDir_ + "/" + archivePath));
}

void Backend::startViewFile(const QString& path) {
  QString parentDir = arqiverDir_;
  if (!arqiverDir_.isEmpty()) {
    QDir dir(arqiverDir_);
    if (path.contains("/")) {
      parentDir = arqiverDir_ + "/" + path.section("/", 0, -2);
      dir.mkpath(parentDir); // also creates "dir" if needed
    }
    else if (!dir.exists())
      dir.mkpath(arqiverDir_);
  }
  QString fileName = (arqiverDir_.isEmpty() ? QDateTime::currentDateTime().toString("yyyyMMddhhmmss")
                                            : parentDir + "/")
                     + path.section("/",-1);
  if (!QFile::exists(fileName)) {
    QString cmnd;
    QStringList args;
    if (isGzip_) {
      cmnd = "gzip";
      args << "-d" << "--to-stdout" << filepath_;
    }
    else if (is7z_) {
      QStringList args;
      if (encrypted_)
        args << "-p" + pswrd_;
      args << "x" << fileArgs_ << "-o" + arqiverDir_;
      args << "-y"; // required with multiple passwords (says yes to the overwrite prompt)
      emit processStarting();
      QProcess tmpProc;
      tmpProc.start ("7z", args);
      while (!tmpProc.waitForFinished(500))
        QCoreApplication::processEvents();
      if (tmpProc.exitCode() != 0)
        pswrd_ = QString(); // avoid overwrite prompt if there are more than one password
      emit processFinished(tmpProc.exitCode() == 0, "");
      if (!QProcess::startDetached("gio", QStringList() << "open" << fileName)) // "gio" is more reliable
        QProcess::startDetached("xdg-open", QStringList() << fileName);
      return;
    }
    else {
      cmnd = TAR_CMD;
      args << "-x" << fileArgs_ << "--include" << path <<"--to-stdout";
    }
    emit processStarting();
    QProcess tmpProc;
    tmpProc.setStandardOutputFile(fileName);
    tmpProc.start(cmnd, args);
    while (!tmpProc.waitForFinished(500))
      QCoreApplication::processEvents();
    emit processFinished(tmpProc.exitCode() == 0, "");
  }
  if (!QProcess::startDetached("gio", QStringList() << "open" << fileName)) // "gio" is more reliable
    QProcess::startDetached("xdg-open", QStringList() << fileName);
}

QString Backend::extractSingleFile(const QString& path) {
  QString parentDir = arqiverDir_;
  if(!arqiverDir_.isEmpty()){
    QDir dir(arqiverDir_);
    if (path.contains("/")) {
      parentDir = arqiverDir_ + "/" + path.section("/", 0, -2);
      dir.mkpath(parentDir); // also creates "dir "if needed
    }
    else if (!dir.exists())
      dir.mkpath(arqiverDir_);
  }
  QString fileName = (arqiverDir_.isEmpty() ? QDateTime::currentDateTime().toString("yyyyMMddhhmmss")
                                          : parentDir + "/")
                     + path.section("/",-1);
  if (!QFile::exists(fileName)) {
    QString cmnd;
    QStringList args;
    if (isGzip_) {
      cmnd = "gzip";
      args << "-d" << "--to-stdout" << filepath_;
    }
    else if (is7z_) {
      QStringList args;
      if (encrypted_ )
        args << "-p" + pswrd_;
      args << "x" << fileArgs_ << "-o" + arqiverDir_ << "-y";
      emit processStarting();
      QProcess tmpProc;
      tmpProc.start ("7z", args);
      while (!tmpProc.waitForFinished(500))
        QCoreApplication::processEvents();
      if (tmpProc.exitCode() != 0)
        pswrd_ = QString();
      emit processFinished(tmpProc.exitCode() == 0, "");
      return fileName;
    }
    else {
      cmnd = TAR_CMD;
      args << "-x" << fileArgs_ << "--include" << path <<"--to-stdout";
    }
    emit processStarting();
    QProcess tmpProc;
    tmpProc.setStandardOutputFile(fileName);
    tmpProc.start(cmnd, args);
    while (!tmpProc.waitForFinished(500))
      QCoreApplication::processEvents();
    emit processFinished(tmpProc.exitCode() == 0, "");
  }
  return fileName;
}

void Backend::parseLines (QStringList& lines) {
  static bool hasParentDir = false;
  if (contents_.isEmpty()) {
    hasParentDir = true;
    archiveParentDir_ = QString();
  }
  if (is7z_) {
    static int Name = 0;
    if (starting7z_) {
      //Ignore all the p7zip header info
      while (starting7z_ && !lines.isEmpty()) {
        if (lines[0] == "--")
          starting7z_ = false; //found the end of the headers
        lines.removeAt(0);
      }
    }
    for (int i = 0; i < lines.length(); i++) {
      if (lines[i].simplified().isEmpty() || lines[i].startsWith("----") || lines[i].startsWith(" = "))
        continue;
      if (LIST) {
        QString file;
        QStringList info = lines[i].split(" ",QString::SkipEmptyParts);
        if(info.size() < 5) continue; // invalid line
        if (lines[i].contains("  Attr  ")) { // header
          Name = lines[i].indexOf(info.at(5));
          continue;
        }
        if (!info.at(2).contains(".")) continue; // bottom line
        // Format: [date, time, attr, size, compressed size, name]
        if(info.size() == 5) {
          file = lines[i].right(lines[i].size() - Name);
          contents_.insert(file, QStringList() << info[2] << info[3] << QString::number(0));
        }
        else { // info.size() == 6
          if (lines[i].at(Name - 3) == ' ')
            info[4] = QString::number(0);
          file = lines[i].right(lines[i].size() - Name);
          contents_.insert(file, QStringList() << info[2] << info[3] << info[4]);
        }
        if (!file.isEmpty()) {
          if (hasParentDir) {
            if(archiveParentDir_.isEmpty()) {
              archiveParentDir_ = file.section('/', 0, 0);
              if(archiveParentDir_.isEmpty())
                hasParentDir = false;
            }
            else if (archiveParentDir_ != file.section('/', 0, 0)) {
              hasParentDir = false;
              archiveParentDir_ = QString();
            }
          }
        }
      }
    }
    return;
  }
  for (int i = 0; i < lines.length(); i++) {
    if (isGzip_) {
      // lines[i] is:                 <compressed_size>                   <uncompressed_size> -33.3% /x/y/z}
       QStringList info = lines[i].split(" ",QString::SkipEmptyParts);
      if (contents_.isEmpty() && info.size() >= 4) {
        int indx = lines[i].indexOf("% ");
        if (indx > -1) {
          QString file = lines[i].mid(indx + 2).section('/', -1);
          if (filepath_.endsWith(".svgz") && file.endsWith(".svgz")) {
            /* WARNING: gzip lists the file as svgz again */
            file.remove(QRegularExpression("svgz$"));
            file += "svg";
          }
          contents_.insert(file,
                           QStringList() << "-rw-r--r--" << info.at(1) << ""); // [perms, size, linkto]
        }
      }
      return;
    }
    QRegularExpressionMatch match;
    int indx = lines[i].indexOf(QRegularExpression("^\\s*x\\s+"), 0 , &match);
    if (indx == 0 && getMimeType(filepath_) == "application/zip") {
      /* ZIP archives may not have all the extra information - just file names */
      QString file = lines[i].right(lines[i].length() - match.capturedLength());
      QString perms = "";
      if(file.endsWith("/")) {
        perms = "d";
        file.chop(1);
      }
      if (hasParentDir) {
        if(archiveParentDir_.isEmpty()) {
          archiveParentDir_ = file.section('/', 0, 0);
          if(archiveParentDir_.isEmpty())
            hasParentDir = false;
        }
        else if (archiveParentDir_ != file.section('/', 0, 0)) {
          hasParentDir = false;
          archiveParentDir_ = QString();
        }
      }
      contents_.insert (file, QStringList() << perms << "-1" << ""); // [perms, size, linkto ]
    }
    // here, lines[i] is like: -rw-r--r--  0 1000   1000        7 Jun 30 23:49 PATH
    indx = lines[i].indexOf(QRegularExpression("(\\S+\\s+){7}\\S+ "), 0 , &match);
    if (indx != 0 || match.capturedLength() == lines[i].length())
      continue; // invalid line
    QStringList info;
    info << lines[i].left(match.capturedLength()).split(" ",QString::SkipEmptyParts); // 8 elements
    info << lines[i].right(lines[i].length() - match.capturedLength());
    // here, info is like ("-rw-r--r--", "1", "0", "0", "645", "Feb", "5", "2016", "x/y -> /a/b")
    QString file = info[8];
    if(file.endsWith("/"))
      file.chop(1);
    QString linkto;
    //See if this file has the "link to" or "->"  notation
    if (file.contains(" -> ")) {
      linkto = file.section(" -> ", 1, -1);
      file = file.section(" -> ", 0, 0);
    }
    else if (file.contains(" link to ")) {
      //Special case - alternate form of a link within a tar archive (not reflected in perms)
      linkto = file.section(" link to ", 1, -1);
      file = file.section(" link to ", 0, 0);
      if(info[0].startsWith("-"))
        info[0].replace(0, 1, "l");
    }
    if (hasParentDir) {
      if(archiveParentDir_.isEmpty()) {
          archiveParentDir_ = file.section('/', 0, 0);
          if(archiveParentDir_.isEmpty())
          hasParentDir = false;
      }
      else if (archiveParentDir_ != file.section('/', 0, 0)) {
          hasParentDir = false;
          archiveParentDir_ = QString();
      }
    }
    contents_.insert(file, QStringList() << info[0] << info[4] << linkto); // [perms, size, linkto ]
  }
}

void Backend::startList(bool withPassword) {
  contents_.clear();
  keyArgs_.clear();
  LIST = true;
  if (isGzip_) {
    keyArgs_ << "-l";
    PROC.start("gzip", QStringList() << "-l" << filepath_);
  }
  else if (is7z_) {
    QStringList args;
    args << "-p" + (withPassword ? pswrd_ : QString()); // needed for encrypted lists
    if (!encryptionQueried_)
      args << "-slt"; // query encryption instead of listing
    args << "l";
    starting7z_ = true;
    keyArgs_ << "l";
    PROC.start("7z", QStringList() << args << fileArgs_);
  }
  else {
    QStringList args;
    args << "-tv";
    keyArgs_ << "-tv";
    PROC.start(TAR_CMD, QStringList() << args << fileArgs_);
  }
}

void Backend::procFinished(int retcode, QProcess::ExitStatus) {
  if (is7z_ && !encryptionQueried_ && keyArgs_.contains("l")) {
    encryptionQueried_ = true;
    if (encryptedList_)
      emit encryptedList(filepath_); // its slot should get password and load the file again
    else {
      /* now, really start listing */
      QStringList args;
      if (!pswrd_.isEmpty()) // happens only when the list is encrypted
        args << "-p" + pswrd_;
      args << "l";
      starting7z_ = true;
      keyArgs_.clear(); keyArgs_ << "l";
      PROC.start("7z", QStringList() << args << fileArgs_);
    }
    return;
  }

  /* NOTE: processFinished() should be emitted once, in the end */
  static QString result;
  processData();
  LIST = false;
  if (is7z_) {
    starting7z_ = false;
    if (keyArgs_.contains("l")) { // listing
      emit loadingFinished();
      if (retcode != 0) {
        contents_.clear();
        result = tr("Could not read archive");
      }
      else if (result.isEmpty()) {
        result = tr("Archive Loaded");
        emit loadingSuccessful();
      }
      emit processFinished((retcode == 0), result);
      result.clear();
    }
    else if (keyArgs_.contains("a") || keyArgs_.contains("d")) { // addition/removal
      result = tr("Modification Finished");
      emit archivingSuccessful();
      if (!encryptedList_) {
        /* We want to know which files are encrypted after an addition.
           After a deletion, it makes no difference. */
        encryptionQueried_ = false;
        encryptedPaths_.clear();
      }
      startList(encryptedList_);
    }
    else { // extraction
      /*if (retcode == 0) {
        QStringList args = PROC.arguments();
        for (int i = 0; i < args.length(); i++) {
          if(args[i].startsWith("-o")) //just extracted to a dir - open it now
            QProcess::startDetached("xdg-open \"" + args[i].section("-o", 1, -1) + "\"");
        }
      }*/
      emit extractionFinished();
      if (retcode == 0)
        emit extractionSuccessful();
      emit processFinished((retcode == 0), result);
      result.clear();
    }
    return;
  }
  if (keyArgs_.contains("-tv") || keyArgs_.contains("-l")) { // listing
    if (retcode != 0) {
      contents_.clear();
      result = tr("Could not read archive");
    }
    else if (result.isEmpty()) {
      emit loadingFinished();
      if (retcode == 0) {
        result = tr("Archive Loaded");
        emit loadingSuccessful();
      }
      if (isGzip_)
        emit archivingSuccessful(); // because the created archive is just loaded (-> startAdd)
    }
    emit processFinished((retcode == 0), result);
    result.clear();
  }
  else
  {
    bool updateList = true;
    if (keyArgs_.contains("-x")) { // extraction
      updateList = false;
      emit extractionFinished();
      if (retcode == 0)
      {
        result = tr("Extraction Finished");
        emit extractionSuccessful();
        /*if (args.count("--include") == 1) {
          //Need to find the full path to the (single) extracted file
          QString path = args.last() +"/"+ args[ args.indexOf("--include") + 1].section("/", -1);
          QFile::setPermissions(path, QFileDevice::ReadOwner);
          QProcess::startDetached("xdg-open  \"" + path + "\"");
        }
        else {
          //Multi-file extract - open the dir instead
          QString dir = args.last();
          //Check to see if tar extracted into a new subdir it just created
          if (QFile::exists(dir + "/" + filepath_.section("/", -1).section(".", 0, 0))) {
            dir = dir + "/" + filepath_.section("/", -1).section(".", 0, 0);
          }
          QProcess::startDetached("xdg-open \"" + dir + "\""); //just extracted to a dir - open it now
        }*/
      }
    }
    else if (keyArgs_.contains("-c")) { // addition/removal
      if (QFile::exists(tmpfilepath_)) {
        if (retcode == 0) {
          QFile::remove(filepath_);
          QFile::rename(tmpfilepath_, filepath_);
        }
        else
          QFile::remove(tmpfilepath_);
      }

      result = tr("Modification Finished");
      if (insertQueue_.isEmpty())
        emit archivingSuccessful();
      else {
        updateList = false;
        QTimer::singleShot(0, this, SLOT(startInsertFromQueue()));
      }
    }
    if (updateList)
      startList();
    else {
      emit processFinished(retcode == 0, result);
      result.clear();
    }
  }
}

void Backend::processData() {
  if (is7z_ && !encryptionQueried_) {
    if (!encryptedList_) {
      QString read = PROC.readAllStandardOutput();
      if (read.contains("\nERROR: ")) { // ERROR: FILE_PATH : Can not open encrypted archive. Wrong password?
        encryptedList_ = encrypted_ = true;
      }
      else {
        const QStringList& items = read.split("\n\n", QString::SkipEmptyParts);
        for (const QString& thisItem : items) {
          if (thisItem.contains("\nEncrypted = +")) {
            /* the archive has an encrypted file but its header isn't encrypted */
            QStringList lines = thisItem.split("\n", QString::SkipEmptyParts);
            if (!lines.isEmpty()) {
              QString pathLine;
              if (lines.at(0).startsWith("Path = "))
                pathLine = lines.at(0);
              else if (lines.at(1).startsWith("Path = ")) // the first line consists of dashes
                pathLine = lines.at(1);
              if (!pathLine.isEmpty())
                encryptedPaths_ << pathLine.remove(0, 7);
            }
            encrypted_ = true;
          }
        }
      }
    }
    return; // no listing here
  }
  static QString data;
  QString read = data + PROC.readAllStandardOutput();
  if (read.endsWith("\n"))
    data.clear();
  else {
    data = read.section("\n", -1);
    read = read.section("\n", 0, -2);
  }
  QStringList lines = read.split("\n", QString::SkipEmptyParts);

  if (isGzip_ && lines.size() == 2) {
    QString last = lines.at(1);
    lines.clear();
    lines << last;
  }

  if (LIST)
    parseLines(lines);
  if (is7z_) {
    if (read.contains("\nERROR")) { // ERROR: Data Error in encrypted file. Wrong password? :
      /* while extracting the archive, another file in it had another password */
      pswrd_ = QString();
    }
    emit progressUpdate(-1, "");
  }
  else {
    QString info;
    if (!lines.isEmpty())
      info = lines.last();
    emit progressUpdate(-1, info);
  }
}

void Backend::onError(QProcess::ProcessError error) {
  if (error == QProcess::FailedToStart)
    emit errorMsg(tr("%1 is missing from your system.\nPlease install it for this kind of archive!")
                  .arg(PROC.program()));
}

}
