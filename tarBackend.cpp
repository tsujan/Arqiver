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

#include "tarBackend.h"
#include <QFile>
#include <QDir>
#include <QDateTime>
#include <QCoreApplication>
#include <QTimer>
#include <QStandardPaths>

#ifdef Q_OS_LINUX
#define TAR_CMD "bsdtar"
#else
#define TAR_CMD "tar"
#endif

namespace Arqiver {

Backend::Backend(QObject *parent) : QObject(parent) {
  PROC.setProcessChannelMode(QProcess::MergedChannels);
  PROC.setProgram(TAR_CMD);
  connect(&PROC, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &Backend::procFinished);
  connect(&PROC, &QProcess::readyReadStandardOutput, this, &Backend::processData);
  connect(&PROC, &QProcess::started, this, &Backend::ProcessStarting);
  LIST = false;
}

Backend::~Backend() {
  if(!arqiverDir_.isEmpty())
    QDir(arqiverDir_).removeRecursively();
}

void Backend::loadFile(const QString& path) {
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

  filepath_ = path;
  tmpfilepath_ = filepath_.section("/", 0, -2) + "/" + ".tmp_arqiver-" + curTime + filepath_.section("/", -1);
  flags_.clear();
  flags_ << "-f" << filepath_; // add the actual archive path
  if(QFile::exists(path))
    startList();
  else {
    contents_.clear();
    emit FileLoaded();
    emit ProcessFinished(true, "");
  }
}

bool Backend::canModify(){
  static QStringList validEXT;
  if (validEXT.isEmpty()) {
    validEXT << ".zip" << ".tar.gz" << ".tgz" << ".tar.xz" << ".txz" << ".tar.bz" << ".tbz" << ".tar.bz2" << ".tbz2" << ".tar" << ".tar.lzma" << ".tlz" << ".cpio" << ".pax" << ".ar" << ".shar" << ".7z";
  }
  for (int i = 0; i < validEXT.length(); i++){
    if (filepath_.endsWith(validEXT[i]))
      return true;
  }
  return false;
}

QString Backend::currentFile(){
  return filepath_;
}

bool Backend::isWorking(){
  return (PROC.state() != QProcess::Running);
}

//Listing routines
QStringList Backend::heirarchy() {
  return contents_.keys();
}

double Backend::size(QString file) {
  if (!contents_.contains(file))
    return -1;
  return contents_.value(file)[1].toDouble();
}

double Backend::csize(QString file) {
  if (!contents_.contains(file))
    return -1;
  return contents_.value(file)[1].toDouble();
}

bool Backend::isDir(QString file) {
  if (!contents_.contains(file))
    return false;
  return contents_.value(file)[0].startsWith("d");
}

bool Backend::isLink(QString file) {
  if (!contents_.contains(file))
    return false;
  return contents_.value(file)[0].startsWith("l");
}

QString Backend::linkTo(QString file) {
  if (!contents_.contains(file))
    return "";
  return contents_.value(file)[2];
}

void Backend::startAdd(QStringList paths,  bool absolutePaths) {
  if (paths.contains(filepath_))
    paths.removeAll(filepath_); // exclude the archive itself
  if(paths.isEmpty())
    return;
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
  args << flags_;
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
    int i = 0;
    while (QFile::exists(tmpfilepath_)) { // practically impossible
      tmpfilepath_ += QString::number(i);
      ++i;
    }
    args.replaceInStrings(filepath_, tmpfilepath_);
    args << "@" + filepath_;
  }
  PROC.start(TAR_CMD, args);
}

void Backend::startRemove(QStringList paths) {
  if (paths.contains(filepath_))
    paths.removeAll(filepath_);
  if (contents_.isEmpty() || paths.isEmpty() || !QFile::exists(filepath_))
    return; //invalid
  QStringList args;
  args << "-c" << "-a";
  args << flags_;
  int i = 0;
  while (QFile::exists(tmpfilepath_)) { // practically impossible
      tmpfilepath_ += QString::number(i);
      ++i;
  }
  args.replaceInStrings(filepath_, tmpfilepath_);
  //Add the include rules for all the files we want to keep (no exclude option in "tar")
  for (int i = 0; i < paths.length(); i++) {
    args << "--exclude" << paths[i];
  }
  args << "@" + filepath_;
  PROC.start(TAR_CMD, args);
}

void Backend::startExtract(QString path, bool overwrite, QString file) {
  startExtract(path, overwrite, QStringList() << file);
}

void Backend::startExtract(QString path, bool overwrite, QStringList files) {
  QStringList args;
  args << "-x" << "--no-same-owner";
  if(!overwrite) // NOTE: We never overwrite in Arqiver. This might be changed later.
    args << "-k";
  args << flags_;
  for (int i = 0; i < files.length(); i++) {
    if (files[i].simplified().isEmpty())
      continue;
    args << "--include" << files[i] << "--strip-components" << QString::number(files[i].count("/"));
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
  args << "-C" << xPath;
  PROC.start(TAR_CMD, args); // doesn't create xPath if not existing
}

void Backend::startViewFile(QString path) {
  QString parentDir = arqiverDir_;
  if(!arqiverDir_.isEmpty()){
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
    QStringList args;
    args << "-x" << flags_ << "--include" << path <<"--to-stdout";
    emit ProcessStarting();
    QProcess tmpProc;
    tmpProc.setStandardOutputFile(fileName);
    tmpProc.start(TAR_CMD,args);
    while (!tmpProc.waitForFinished(500))
      QCoreApplication::processEvents();
    emit ProcessFinished(tmpProc.exitCode() == 0, "");
  }
  if (!QProcess::startDetached ("gio", QStringList() << "open" << fileName)) // "gio" is more reliable
    QProcess::startDetached("xdg-open", QStringList() << fileName);
}

QString Backend::extractFile(QString path) {
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
    QStringList args;
    args << "-x" << flags_ << "--include" << path <<"--to-stdout";
    emit ProcessStarting();
    QProcess tmpProc;
    tmpProc.setStandardOutputFile(fileName);
    tmpProc.start(TAR_CMD,args);
    while (!tmpProc.waitForFinished(500))
      QCoreApplication::processEvents();
    emit ProcessFinished(tmpProc.exitCode() == 0, "");
  }
  return fileName;
}

void Backend::parseLines (QStringList lines) {
  static bool hasParentDir = false;
  if (contents_.isEmpty()) {
    hasParentDir = true;
    archiveParentDir_ = QString();
  }
  for (int i = 0; i < lines.length(); i++) {
    QStringList info = lines[i].split(" ",QString::SkipEmptyParts);
    //Format: [perms, ?, user, group, size, month, day, time, file]
    if (info.startsWith("x ") && filepath_.endsWith(".zip")) {
      //ZIP archives do not have all the extra information - just filenames
      while (info.length() > 2)
        info[1] = info[1] + " " + info[2];
      QString file = info[1];
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
        else if (archiveParentDir_ != file.section('/', 0, 0)){
          hasParentDir = false;
          archiveParentDir_ = QString();
        }
      }
      contents_.insert (file, QStringList() << perms << "-1" <<""); //Save the [perms, size, linkto ]
    }
    else if (info.length() < 9) continue; //invalid line
    //TAR Archive parsing
    while (info.length() > 9) {
      info[8] = info[8] + " " + info[9];
      info.removeAt(9); //Filename has spaces in it
    }
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
      else if (archiveParentDir_ != file.section('/', 0, 0)){
          hasParentDir = false;
          archiveParentDir_ = QString();
      }
    }
    contents_.insert(file, QStringList() << info[0] << info[4] << linkto); //Save the [perms, size, linkto ]
  }
}

void Backend::startList() {
  contents_.clear();
  QStringList args;
  args << "-tv";
  LIST = true;
  PROC.start(TAR_CMD, QStringList() << args << flags_);
}

void Backend::procFinished(int retcode, QProcess::ExitStatus) {
  static QString result;
  processData();
  LIST = false;
  if (PROC.arguments().contains("-tv")) {
    if (retcode != 0) {  //could not read archive
      contents_.clear();
      result = tr("Could not read archive");
    }
    else if (result.isEmpty()) {
      result = tr("Archive Loaded");
      emit FileLoaded();
    }
    emit ProcessFinished((retcode == 0), result);
    result.clear();
  }
  else
  {
    bool needupdate = true;
    QStringList args = PROC.arguments();
    if (args.contains("-x") && retcode == 0) {
      needupdate=false;
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
    else if (args.contains("-c") && QFile::exists(tmpfilepath_)) {
      if (retcode == 0) {
        QFile::remove(filepath_);
        QFile::rename(tmpfilepath_, filepath_);
      }
      else
        QFile::remove(tmpfilepath_);
    }
    if (args.contains("-x")) {
      result = tr("Extraction Finished");
      emit ExtractSuccessful();
    }
    else if (args.contains("-c")) {
      result = tr("Modification Finished");
      if (insertQueue_.isEmpty())
        emit ArchivalSuccessful();
      else {
        needupdate = false;
        QTimer::singleShot(0, this, SLOT(startInsertFromQueue()));
      }
    }
    if (needupdate)
      startList();
    else {
      emit ProcessFinished(retcode == 0, result);
      result.clear();
    }
  }
}

void Backend::processData() {
  static QString data;
  QString read = data + PROC.readAllStandardOutput();
  if (read.endsWith("\n"))
    data.clear();
  else {
    data = read.section("\n", -1);
    read = read.section("\n", 0, -2);
  }
  QStringList lines = read.split("\n",QString::SkipEmptyParts);
  QString info;
  if (LIST)
    parseLines(lines);
  if (!lines.isEmpty())
    info = lines.last();
  emit ProgressUpdate(-1, info);
}

}
