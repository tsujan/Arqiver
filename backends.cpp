/*
 * Copyright (C) Pedram Pourang (aka Tsu Jan) 2018-2025 <tsujan2000@gmail.com>
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
#include <QUrl>
#include <QCoreApplication>
#include <QTimer>
#include <QStandardPaths>
#include <QDesktopServices>
#include <QMimeDatabase>
#include <QMimeData>
#include <QRegularExpression>

#ifdef Q_OS_LINUX
#define TAR_CMD "bsdtar"
#else
#define TAR_CMD "tar"
#endif

namespace Arqiver {

static const QRegularExpression newlineExp("(?<!\\\\)\\\\n");
static const QRegularExpression tabExp("(?<!\\\\)\\\\t");
static const QRegularExpression startBslashExp("(^|/)\\\\"); // used with startBackslash_

Backend::Backend(QObject *parent) : QObject(parent) {
  tarCmnd_ = TAR_CMD;
  proc_.setProcessChannelMode(QProcess::MergedChannels);
  connect(&proc_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &Backend::procFinished);
  connect(&proc_, &QProcess::readyReadStandardOutput, this, &Backend::processData);
  connect(&proc_, &QProcess::started, this, &Backend::processStarting);
  connect(&proc_, &QProcess::errorOccurred, this, &Backend::onError);

  isKilled_ = false;
  listing_ = false;
  isGzip_ = is7z_ = false;
  starting7z_ = encryptionQueried_ = encrypted_ = encryptedList_ = false;
  startBackslash_ = false; // used only with bsdtar in "Backend::startExtract()"

  watcher_ = new QFileSystemWatcher(this);
  connect(watcher_, &QFileSystemWatcher::fileChanged, [this](const QString& path) {
    /* WARNING: If the parent directory of an extracted file contains unextracted file(s),
                bsdtar resets the modification time of the file on extracting the parent
                directory with the "-k" option, and so, the watcher emits "fileChanged()".
                As a workaround, we keep track of modification times. */
    if (QFile::exists(path)) {
      bool emitSignal(isGzip_ || is7z_ || !modTimes_.contains(path));
      if (!emitSignal) {
        QDateTime modTime = QFileInfo(path).lastModified();
        if (modTimes_.value(path) < modTime) {
          emitSignal = true;
          modTimes_.insert(path, modTime);
        }
      }
      if (emitSignal) {
        emit fileModified(true);
        if (!changedFiles_.contains(path))
          changedFiles_ << path;
      }
      if (!watcher_->files().contains(path)){
        watcher_->addPath(path); // recommended by Qt doc
      }
    }
    else
      modTimes_.remove(path);
  });
}

Backend::~Backend() {
  if (!arqiverDir_.isEmpty())
    QDir(arqiverDir_).removeRecursively();
}

void Backend::setTarCommand(const QString& cmnd) {
#ifdef Q_OS_LINUX
  Q_UNUSED(cmnd)
  tarCmnd_ = TAR_CMD;
#else
  if (cmnd.isEmpty())
    tarCmnd_ = TAR_CMD;
  else
    tarCmnd_ = cmnd;
#endif
}

QString Backend::getMimeType(const QString &fname) {
  QString mimeType, suffix;
  int left = fname.indexOf(QLatin1Char('.'));
  if (left != -1) {
    if (fname.compare("CMakeLists.txt", Qt::CaseInsensitive) == 0) // exception
      mimeType = "text/x-cmake";
    else {
      suffix = fname.right(fname.size() - left - 1);
      mimeType = mimeTypes_.value(suffix);
    }
  }
  if (mimeType.isEmpty()) {
    QMimeDatabase mimeDatabase;
    mimeType = mimeDatabase.mimeTypeForFile(QFileInfo(fname)).name();
    if (!suffix.isEmpty())
      mimeTypes_.insert(suffix, mimeType);
  }
  return mimeType;
}

void Backend::loadFile(const QString& path, bool withPassword) {
  /* check if the file extraction directory can be made,
     but don't create it until a file is viewed */
  const QString curTime = QDateTime::currentDateTime().toString("yyyyMMddhhmmsszzz");
  if (!arqiverDir_.isEmpty()) {
    watcher_->removePaths(watcher_->files());
    changedFiles_.clear();
    modTimes_.clear();
    emit fileModified(false);
    QDir(arqiverDir_).removeRecursively();
  }
  QString cache = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
  if (!cache.isEmpty()) {
    QDir cacheDir(cache);
    if (cacheDir.exists()) {
      arqiverDir_ = cache + "/" + "arqiver-" + curTime;
    }
  }

  /*
     NOTE: So far, bsdtar, gzip and 7z are supported.
           Also, password protected 7z archives can be extracted and created.
           7z has two kinds of prompts that we avoid: password and overwrite prompts.
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
  else if (mt == "application/x-7z-compressed"
           || mt == "application/x-ms-dos-executable" || mt == "application/vnd.microsoft.portable-executable" || mt == "application/x-msdownload" || mt == "application/x-msi"
           || mt == "application/vnd.ms-cab-compressed" || mt == "application/vnd.rar"
           || mt == "application/x-cd-image" || mt == "application/vnd.efi.iso" || mt == "application/vnd.appimage"
           || mt == "application/x-xz"
           || mt == "application/zstd"
           //|| mt == "application/x-ace"
           || mt == "application/x-bzip" || mt == "application/x-bzip1" || mt == "application/x-bzip2" || mt == "application/x-bzpdf" || mt == "application/x-xzpdf"
           || mt == "application/x-apple-diskimage") {
    is7z_ = true; isGzip_ = false;
  }
  else if (mt == "application/x-raw-disk-image" || mt == "application/vnd.efi.img"
           || mt == "application/vnd.snap") {
    /* 7z can't open compressed disk images, and bsdtar can't handle uncompressed ones */
    QMimeDatabase mimeDatabase;
    QString realMt = mimeDatabase.mimeTypeForFile(QFileInfo(path), QMimeDatabase::MatchContent).name();
    if (realMt == "application/gzip" || realMt == "application/x-cpio" || realMt == "application/x-xz") {
      isGzip_ = is7z_ = false;
    }
    else {
      is7z_ = true; isGzip_ = false;
    }
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
  if (QFile::exists(path))
    startList(withPassword);
  else {
    if (is7z_)
      encryptionQueried_ = true; // an empty archive doesn't have encryption (yet)
    contents_.clear();
    startBackslash_ = false;
    emit loadingFinished();
    emit loadingSuccessful();
    emit processFinished(true, QString());
  }
}

bool Backend::canModify(bool *canUpdate) const {
  static QStringList validMimeTypes;
  if (validMimeTypes.isEmpty()) {
    validMimeTypes << "application/zip" << "application/x-compressed-tar" << "application/x-gzpdf" << "image/svg+xml-compressed" << "application/x-xz-compressed-tar" << "application/x-bzip-compressed-tar" << "application/x-bzip1-compressed-tar" << "application/x-bzip2-compressed-tar" << "application/x-tar" << "application/x-lzma-compressed-tar" << "application/x-zstd-compressed-tar" << "application/x-cpio" << /*".pax" <<*/ "application/x-archive" << /*".shar" <<*/ "application/gzip" << "application/x-7z-compressed";
  }
  static QStringList updatableMimeTypes;
  if (updatableMimeTypes.isEmpty()) {
    updatableMimeTypes << /*"application/zstd" << */"application/x-bzip" << "application/x-bzip1" << "application/x-bzip2" << "application/x-xz" << "application/vnd.debian.binary-package" << "application/x-rpm" << "application/x-source-rpm";
  }
  QMimeDatabase mimeDatabase;
  QString mimeTypeName = mimeDatabase.mimeTypeForFile(QFileInfo(filepath_)).name();
  bool res = archiveSingleRoot_ != "."  // not like some rpm archives
             && validMimeTypes.contains(mimeTypeName);
  *canUpdate = (archiveSingleRoot_ != "." && (res || updatableMimeTypes.contains(mimeTypeName)));
  return res;
}

QString Backend::currentFile() const {
  return filepath_;
}

QStringList Backend::hierarchy() const {
  return contents_.keys();
}

QString Backend::singleRoot() const {
  return archiveSingleRoot_;
}

QString Backend::sizeString(const QString& file) const {
  if (contents_.contains(file))
    return contents_.value(file).at(1);
  return QString();
}

double Backend::size(const QString& file) const {
  if (!contents_.contains(file))
    return -1;
  return contents_.value(file).at(1).toDouble();
}

double Backend::csize(const QString& file) const {
  if (!contents_.contains(file))
    return -1;
  if (is7z_)
    return contents_.value(file).at(2).toDouble();
  return contents_.value(file).at(1).toDouble();
}

bool Backend::isDir(const QString& file) const {
  if (!contents_.contains(file))
    return false;
  if (is7z_)
    return contents_.value(file).at(0).startsWith("D");
  return contents_.value(file).at(0).startsWith("d");
}

bool Backend::isLink(const QString& file) const {
  if (!contents_.contains(file))
    return false;
  return contents_.value(file).at(0).startsWith("l");
}

QString Backend::linkTo(const QString& file) const {
  if (!contents_.contains(file))
    return QString();
  return contents_.value(file).at(2);
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

// This is only for bsdtar, which uses an ed-like pattern substitution.
// WARNING: Here it is supposed that backslashes are escaped by bsdtar.
static inline QString escapeSpecialChars(const QString& str) {
  QString newStr = str;
  newStr.replace("*", "\\*").replace("[", "\\[").replace("^", "\\^")
        .replace("$", "\\$").replace("?", "\\?");
  return newStr;
}

void Backend::updateArchive() {
  if (arqiverDir_.isEmpty() || changedFiles_.isEmpty()
      || !QFile::exists(filepath_)) {
    return;
  }
  QDir dir(arqiverDir_);
  if (!dir.exists()) return;

  emit fileModified(false);
  keyArgs_.clear();
  QStringList args;

  if (is7z_) {
    // only add the top files/folders of the temporary directory; 7z takes care of the rest
    QStringList paths;
    const QStringList entries = dir.entryList(QDir::NoDotAndDotDot | QDir::Dirs | QDir::Files);
    if (entries.isEmpty()) return;
    for (const QString &str : entries)
      paths << arqiverDir_ + "/" + str;
    if (encryptedList_) {
      if (pswrd_.isEmpty())
        return;
      args << "-mhe=on" << "-p" + pswrd_;
    }
    else if (!pswrd_.isEmpty()) {
      args << "-p" + pswrd_;
      encrypted_ = true;
    }
    args << "a" << fileArgs_ << paths;
    starting7z_ = true;
    keyArgs_ << "a";
    proc_.start ("7z", args);
    return;
  }

  emit processStarting();

  if (isGzip_) {
    args << "--to-stdout" << "--force" << changedFiles_.at(0);
    tmpProc_.setStandardOutputFile(filepath_);
    tmpProc_.start("gzip", args);
    if (tmpProc_.waitForStarted()) {
      while (!tmpProc_.waitForFinished(500))
        QCoreApplication::processEvents();
    }
    emit processFinished(tmpProc_.exitCode() == 0, QString());
    return;
  }

  // first remove the files from the archive, waiting until the process is finished...
  args << "-c" << "-a";
  skipExistingFiles(tmpfilepath_); // practically not required
  args << "-f" << tmpfilepath_;
  for (const QString &str : std::as_const(changedFiles_)) {
    /* WARNING: Since the workaround for bsdtar's escaped backslashes is already applied,
                they need to be escaped again, before other special characters are escaped. */
    args << "--exclude" << "^" + escapeSpecialChars(str.section('/', 3).replace("\\", "\\\\"));
  }
  args << "@" + filepath_;
  tmpProc_.start(tarCmnd_, args);
  if (tmpProc_.waitForStarted()) {
    while (!tmpProc_.waitForFinished(500))
      QCoreApplication::processEvents();
  }
  if (QFile::exists(tmpfilepath_)) {
    if (tmpProc_.exitCode() == 0) {
      QFile::remove(filepath_);
      QFile::rename(tmpfilepath_, filepath_);
    }
    else
      QFile::remove(tmpfilepath_);
  }
  emit processFinished(tmpProc_.exitCode() == 0, QString());
  if (tmpProc_.exitCode() != 0)
    return;

  // ... then add the modified files to the archive with a proper parent path
  startAdd(changedFiles_, arqiverDir_);
}

void Backend::startAdd(const QStringList& paths, const QString& parentPath, bool absolutePaths) {
  keyArgs_.clear();
  QStringList filePaths = paths;
  /* exclude the archive itself */
  if (filePaths.contains(filepath_))
    filePaths.removeAll(filepath_);
  /* no path should be a parent folder of the archive */
  QString parentDir = filepath_.section("/", 0, -2);
  for (int i = 0; !filePaths.isEmpty() && i < filePaths.length(); i++) {
    if (parentDir.startsWith(filePaths.at(i))) {
      filePaths.removeAt(i);
      i--;
    }
  }

  if (filePaths.isEmpty()) return;
  /* no path should be repeated */
  filePaths.removeDuplicates();

  QStringList args;
  if (isGzip_) {
    emit processStarting();
    if (QFile::exists(filepath_)) // the overwrite prompt should be already accepted
      args << "--to-stdout" << "--force" << filePaths.at(0);
    else
      args << "--to-stdout" << filePaths.at(0);
    tmpProc_.setStandardOutputFile(filepath_);
    tmpProc_.start("gzip", args); // "gzip -c (-f) file > archive.gz"
    if (tmpProc_.waitForStarted()) {
      while (!tmpProc_.waitForFinished(500))
        QCoreApplication::processEvents();
    }
    emit processFinished(tmpProc_.exitCode() == 0, QString());
    loadFile(filepath_);
    return; // FIXME: Unfortunately, onError() can't be called here
  }
  if (is7z_) {
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
    args << "a" << fileArgs_ << filePaths;
    starting7z_ = true;
    keyArgs_ << "a";
    proc_.start ("7z", args);
    return;
  }
  /* NOTE: All paths should have the same parent directory.
           Check that, and put the wrong paths into insertQueue_.
           (For now, "parentPath" is used only in "updateArchive".) */
  const QString parent = parentPath.isEmpty() ? filePaths.at(0).section("/", 0, -2) : parentPath;
  insertQueue_.clear();
  if (parentPath.isEmpty()) {
    for (int i = 1; i < filePaths.length(); i++) {
      if (filePaths.at(i).section("/", 0, -2) != parent) {
        insertQueue_ << filePaths.takeAt(i);
        i--;
      }
    }
  }
  args << "-c" << "-a";
  bool fileExists(QFile::exists(filepath_));
  if (fileExists) { // use a temporary file to append to the existing archive
    skipExistingFiles(tmpfilepath_); // practically not required
    args << "-f" << tmpfilepath_;
  }
  else
    args << fileArgs_;
  /* now, setup the parent dir */
  if (!absolutePaths) {
    for (int i = 0; i < filePaths.length(); i++) {
      filePaths[i] = filePaths[i].section(parent, 1, -1);
      if (filePaths[i].startsWith("/"))
        filePaths[i].remove(0, 1);
    }
    args << "-C" << parent;
  }
  else
    args << "-C" << "/";
  args << filePaths;
  if (fileExists) // append to the existing archive
    args << "@" + filepath_;
  keyArgs_ << "-c" << "-a" << "-C";
  proc_.start(tarCmnd_, args);
}

void Backend::startRemove(const QStringList& paths) {
  if (!QFile::exists(filepath_)) return;

  keyArgs_.clear();
  if (isGzip_) return;
  QStringList filePaths = paths;
  if (filePaths.contains(filepath_))
    filePaths.removeAll(filepath_);
  if (contents_.isEmpty() || filePaths.isEmpty() || !QFile::exists(filepath_))
    return; // invalid
  filePaths.removeDuplicates();

  // a modified file may be removed
  if (!changedFiles_.isEmpty()) {
    for (const QString &str : std::as_const(filePaths))
      changedFiles_.removeOne(arqiverDir_ + "/" + str);
    if (changedFiles_.isEmpty())
      emit fileModified(false);
  }

  QStringList args;
  if (is7z_) {
    if (encrypted_)
      args << "-p" + pswrd_;
    args << "d" << fileArgs_ << filePaths;
    starting7z_ = true;
    keyArgs_ << "d";
    proc_.start("7z", args);
    return;
  }
  args << "-c" << "-a";
  skipExistingFiles(tmpfilepath_); // practically not required
  args << "-f" << tmpfilepath_;
  for (int i = 0; i < filePaths.length(); i++) {
    args << "--exclude" << "^" + escapeSpecialChars(filePaths.at(i));
  }
  args << "@" + filepath_;
  keyArgs_ << "-c" << "-a" << "--exclude";
  proc_.start(tarCmnd_, args);
}

void Backend::startExtract(const QString& path, const QString& file, bool overwrite, bool preservePaths) {
  startExtract(path, QStringList() << file, overwrite, preservePaths);
}

void Backend::startExtract(const QString& path, const QStringList& files, bool overwrite, bool preservePaths) {
  if (!QFile::exists(filepath_)) return;

  keyArgs_.clear();
  if (isGzip_) {
    /* if the extraction takes place in the same directory, we could do it
       in the usual way, but the standard output method works in all cases */
    /*if (0 && path == filepath_.section("/", 0, -2)) {
      proc_.start("gzip", QStringList() << "-d" << "-k" << filepath_);
      return;
    }*/
    emit processStarting();
    QString extName = filepath_.section("/", -1);
    if (extName.contains(".")) {
      extName = extName.section(".", 0, -2);
    }
    if (extName.isEmpty())
      extName = "arqiver-extracted";
    if (filepath_.endsWith(".svgz") && !extName.endsWith(".svg"))
      extName += ".svg";
    extName = path + "/" + extName;
    skipExistingFiles(extName);
    tmpProc_.setStandardOutputFile(extName);
    tmpProc_.start("gzip", QStringList() << "-d" << "--to-stdout" << filepath_); // gzip -d -c archive.gz > file
    if (tmpProc_.waitForStarted()) {
      while (!tmpProc_.waitForFinished(500))
        QCoreApplication::processEvents();
    }
    emit processFinished(tmpProc_.exitCode() == 0, QString());
    emit extractionFinished();
    emit extractionSuccessful();
    return;
  }

  QStringList args;
  QStringList filesList = files;
  filesList.removeAll(QString());
  if (!filesList.isEmpty()) {
    filesList.removeDuplicates();
    /* the paths may contain newlines, which have been escaped and are restored here */
    filesList.replaceInStrings(newlineExp, "\n").replaceInStrings(tabExp, "\t");
  }

  bool noFileList(filesList.isEmpty());

  if (is7z_) {
    if (noFileList)
      args << "-aou"; // auto-rename: the archive may contain files with identical names
    else if (overwrite)
      args << "-aoa"; // overwrite without prompt
    else
      args << "-aos"; // skip extraction of existing files
    if (encrypted_)
      args << "-p" + pswrd_;
    args << (preservePaths ? "x" : "e") << fileArgs_;
    keyArgs_ << "x" << "e";
    starting7z_ = true;
  }
  else {
    args << "-x" << "--no-same-owner";
    if (!overwrite)
      args << "-k";
    args << fileArgs_;
    if (!noFileList) {
        /* If a file comes after its containing folder in the command line,
           bsdtar doesn't extract the folder. So, we sort the list and read it inversely. */
        filesList.sort();
        int N = filesList.length();
        for (int i = 0; i < N; i++) {
          if (filesList[N - 1 - i].simplified().isEmpty()
              // see the end of this function for the reason
              || (startBackslash_ && filesList[N - 1 - i].contains(startBslashExp))) {
            filesList.removeAt(N - 1 - i);
            continue;
          }
          args << "--include" << escapeSpecialChars(filesList[N - 1 - i])
               << "--strip-components" << QString::number(filesList[N - 1 - i].count("/"));
        }
    }
    keyArgs_ << "-x";
  }

  QString xPath = path;
  bool archiveRootChanged(false);

  /* Prevent overwriting by making an appropriate directory and extracting into it
     if the whole archive is going to be extracted (otherwise, overwriting should be handled by GUI) */
  if (noFileList) {
    QString archiveSingleRoot = archiveSingleRoot_;
    if (!archiveSingleRoot.isEmpty() && archiveSingleRoot.startsWith(".")) {
      archiveSingleRoot.remove(0, 1); // no hidden extraction folder or single hidden extracted file
      archiveRootChanged = true;
    }
    if (!archiveSingleRoot.isEmpty()) { // is empty with some rpm archives or when an encrypted list isn't known yet
      archiveSingleRoot.replace(newlineExp, "\n").replace(tabExp, "\t");
      if (!isGzip_ && !is7z_)
        archiveSingleRoot.replace("\\\\", "\\"); // WARNING: bsdtar escapes backslashes.
      if (QFile::exists(xPath + "/" + archiveSingleRoot)) {
        archiveRootChanged = true;
        QDir dir (xPath);
        QString subdirName = archiveSingleRoot;
        xPath += "/" + subdirName;

        int i = 0;
        QString suffix;
        while (QFile::exists(xPath + suffix)) {
          suffix = "-" + QString::number(i);
          ++i;
        }
        xPath += suffix;
        subdirName += suffix;

        dir.mkdir(subdirName);
      }
      else if (archiveRootChanged) {
        QDir dir (xPath);
        xPath += "/" + archiveSingleRoot;
        dir.mkdir(archiveSingleRoot);
      }
    }
    else { // the archive doesn't have a parent dir and isn't a single file
      archiveRootChanged = false;
      QDir dir(xPath);
      QString subdirName = filepath_.section("/", -1);
      if (subdirName.contains(".")) {
        subdirName = subdirName.section(".", 0, -2);
        if (subdirName.endsWith(".tar"))
          subdirName = subdirName.section(".", 0, -2);
      }
      if (subdirName.isEmpty())
        subdirName = "arqiver-extracted";
      xPath += "/" + subdirName;

      int i = 0;
      QString suffix;
      while (QFile::exists(xPath + suffix)) {
        suffix = "-" + QString::number(i);
        ++i;
      }
      xPath += suffix;
      subdirName += suffix;

      dir.mkdir(subdirName);
    }
  }

  if (is7z_) {
    args << "-o" + xPath;
    if (!noFileList)
      args << filesList;
    proc_.start("7z", args);
  }
  else {
    if (!noFileList && filesList.isEmpty())
      return; // it was emptied above
    args << "-C" << xPath;
    /* WARNING: Because of a nasty bug in bsdtar, "--strip-components" removes
                the start backslash, and so, it can result in a mess. */
    if (!startBackslash_ && archiveRootChanged && contents_.size() > 1)
      args << "--strip-components" << "1"; // the parent name is changed
    keyArgs_ << "-C";
    proc_.start(tarCmnd_, args); // doesn't create xPath if not existing
  }
}

bool Backend::isEncryptedPath(const QString& path) const {
  if (!is7z_) return false;
  return (encryptedList_ || encryptedPaths_.contains(path));
}

// Used only with encrypted archives
bool Backend::isSingleExtracted(const QString& archivePath) const {
  if (arqiverDir_.isEmpty()) return false;
  QString str = archivePath;
  if (str.startsWith("./")) // as with some rpm archives
    str.remove(0, 2);
  return (!str.isEmpty() && QFile::exists(arqiverDir_ + "/" + str));
}

bool Backend::allChildrenExyracted(const QString& parent) const {
  if (arqiverDir_.isEmpty()) return false;
  const QStringList files = contents_.keys();
  if (isDir(parent) || !files.contains(parent)) {
    QString p = parent + "/";
    for (const auto &file : files) {
      if (file != p && file.startsWith(p)) {
        QString str = file;
        if (str.startsWith("./")) // as with some rpm archives
          str.remove(0, 2);
        if (!isGzip_ && !is7z_)
          str.replace("\\\\", "\\"); // WARNING: bsdtar escapes backslashes.
        if (!QFile::exists(arqiverDir_ + "/" + str))
          return false;
      }
    }
  }
  return true;
}

static inline bool removeDir(const QString &dirName)
{
  bool res = true;
  QDir dir(dirName);
  if (dir.exists()) {
      const QFileInfoList infoList = dir.entryInfoList(QDir::Files | QDir::AllDirs
                                                       | QDir::NoDotAndDotDot | QDir::System | QDir::Hidden,
                                                       QDir::DirsFirst);
      for (const QFileInfo& info : infoList) {
        if (info.isDir())
          res = removeDir(info.absoluteFilePath());
        else
          res = QFile::remove(info.absoluteFilePath());
        if (!res) return res;
      }
      res = dir.rmdir(dirName);
  }
  return res;
}

void Backend::removeSingleExtracted(const QString& archivePath) const {
  if (!arqiverDir_.isEmpty()) {
    const QString filePath = arqiverDir_ + "/" + archivePath;
    if (QFile::exists(filePath)) {
      QFileInfo info(filePath);
      if (info.isDir())
        removeDir(filePath);
      else
        QFile::remove(filePath);
    }
  }
}

// Returns false only when a password is needed but it's nonempty and wrong.
bool Backend::startViewFile(const QString& path) {
  if (!QFile::exists(filepath_))
    return true; // "false" is reserved for password of 7z

  bool res = true;

  /* the path may contain newlines, which have been escaped and are restored here */
  QString realPath(path);
  realPath.replace(newlineExp, "\n").replace(tabExp, "\t");

  QString parentDir = arqiverDir_;
  if (!arqiverDir_.isEmpty()) {
    QDir dir(arqiverDir_);
    if (realPath.contains("/")) {
      parentDir = arqiverDir_ + "/" + realPath.section("/", 0, -2);
      dir.mkpath(parentDir); // also creates "dir" if needed
    }
    else if (!dir.exists())
      dir.mkpath(arqiverDir_);
  }
  QString fileName = (arqiverDir_.isEmpty() ? QDateTime::currentDateTime().toString("yyyyMMddhhmmsszzz")
                                            : parentDir + "/")
                     + realPath.section("/",-1);
  if (!isGzip_ && !is7z_)
    fileName.replace("\\\\", "\\"); // WARNING: bsdtar escapes backslashes.
  QFile file(fileName);
  bool fileExists(file.exists());
  if (fileExists && file.size() == static_cast<qint64>(0)) {
    watcher_->removePath(fileName);
    modTimes_.remove(fileName);
    file.remove();
    fileExists = false;
  }
  if (!fileExists) {
    QStringList args;
    if (is7z_) {
      args << "-aou"; // the archive may contain files with identical names
      if (encrypted_)
        args << "-p" + pswrd_;
      args << "x" << fileArgs_ << "-o" + arqiverDir_;
      args << "-y"; // required with multiple passwords (says yes to the overwrite prompt)
      args << realPath;
      emit processStarting();
      tmpProc_.setStandardOutputFile(QProcess::nullDevice());
      tmpProc_.start("7z", args);
      if (tmpProc_.waitForStarted()) {
        while (!tmpProc_.waitForFinished(500))
          QCoreApplication::processEvents();
      }
      res = tmpProc_.exitCode() == 0 || pswrd_.isEmpty();
      if (!res)
        pswrd_ = QString(); // avoid overwrite prompt if there are more than one password
      emit processFinished(tmpProc_.exitCode() == 0, QString());
      if (tmpProc_.exitCode() != 0)
        return res;
    }
    else {
      QString cmnd;
      if (isGzip_) {
        cmnd = "gzip";
        args << "-d" << "--to-stdout" << filepath_;
        emit processStarting();
        tmpProc_.setStandardOutputFile(fileName);
      }
      else { // bsdtar
        cmnd = tarCmnd_;
        // "--fast-read" interferes with extraction of directories
        if (isDir(path))
          args << "-x" << "--no-same-owner";
        else
          args << "-x" << "--no-same-owner" << "--fast-read";
        args << "-k" << fileArgs_
             << "--include" << escapeSpecialChars(realPath) << "-C" << arqiverDir_;
        /* NOTE: The following arguments were also possible with "fileName" as the standard output
                 (as with Gzip) but symlinks couldn't be handled in that way. */
        //args << "-x" << fileArgs_ << "--include" << escapeSpecialChars(realPath) << "--to-stdout";
        emit processStarting();
        tmpProc_.setStandardOutputFile(QProcess::nullDevice());
      }
      tmpProc_.start(cmnd, args);
      if (tmpProc_.waitForStarted()) {
        while (!tmpProc_.waitForFinished(500))
          QCoreApplication::processEvents();
      }
      emit processFinished(tmpProc_.exitCode() == 0, QString());
      if (tmpProc_.exitCode() != 0)
        return true;
    }
    // handle links
    if (!QFileInfo::exists(fileName)) {
      errorMsg(tr("This file is a link but its target does not exist."));
      return res;
    }
  }
  else
    emit processFinished(true, QString());

  // Since the file may be inside a directory extracted by "extractTempFiles()",
  // we may need to add it to the watch list also when it already exists.
  QFileInfo info(fileName);
  if (!info.isDir()) {
    const QString finalTarget = info.canonicalFilePath();
    if (finalTarget == fileName || finalTarget.startsWith(arqiverDir_ + QLatin1Char('/'))) {
      watcher_->addPath(finalTarget);
      if (!isGzip_ && !is7z_)
        modTimes_.insert(finalTarget, info.lastModified());
    }
  }

  if (QStandardPaths::findExecutable("gio").isEmpty()
      || !QProcess::startDetached("gio", QStringList() << "open" << fileName)) { // "gio" is more reliable
    QDesktopServices::openUrl(QUrl::fromLocalFile(fileName));
  }
  return res;
}

void Backend::extractTempFiles(const QStringList& paths) {
  if (!QFile::exists(filepath_)) {
    emit processFinished(true, QString());
    emit tempFilesExtracted(QStringList()); // needed with drag-and-drop
    return;
  }

  QStringList realPaths(paths);
  realPaths.removeAll(QString());
  if (!realPaths.isEmpty()) {
    realPaths.removeDuplicates();
    realPaths.replaceInStrings(newlineExp, "\n").replaceInStrings(tabExp, "\t");
  }
  else { // should never happen in practice
    emit processFinished(true, QString());
    emit tempFilesExtracted(QStringList());
  }

  QStringList tempFileNames;
  if (!arqiverDir_.isEmpty()) {
    QDir dir(arqiverDir_);
    if (!dir.exists())
      dir.mkpath(arqiverDir_);
    QList<QString>::iterator it = realPaths.begin();
    while (it != realPaths.end()) {
      QString realPath = *it;
      if (realPath.simplified().isEmpty()) {
        it = realPaths.erase(it);
        continue;
      }
      if (realPath.startsWith("./") || realPath == ".") { // as with some rpm archives
        realPath.remove(0, 2);
        if (realPath.isEmpty()) {
          it = realPaths.erase(it);
          continue;
        }
      }
      QString parentDir;
      if (realPath.contains("/")) {
        parentDir = arqiverDir_ + "/" + realPath.section("/", 0, -2);
        dir.mkpath(parentDir);
      }
      else
        parentDir = arqiverDir_; // top-level
      if (!isGzip_ && !is7z_)
        realPath.replace("\\\\", "\\"); // WARNING: bsdtar escapes backslashes.
      QString fileName = parentDir + "/" + realPath.section("/",-1);
      tempFileNames << fileName;
      /* check whether it's already extracted (FIXME: this doesn't cover all symlinks) */
      if (QFile::exists(fileName)
          && allChildrenExyracted(*it)) { // realPath may have changed above
        it = realPaths.erase(it);
      }
      else ++it;
    }
  }

  if (!realPaths.isEmpty()) {
    QStringList args;

    if (isGzip_) {
      if (tempFileNames.size() == 1) {
        args << "-d" << "--to-stdout" << filepath_;
        emit processStarting();
        tmpProc_.setStandardOutputFile(tempFileNames.at(0));
        tmpProc_.start("gzip", args);
        if (tmpProc_.waitForStarted()) {
          while (!tmpProc_.waitForFinished(500))
            QCoreApplication::processEvents();
        }
        emit processFinished(tmpProc_.exitCode() == 0, QString());
        emit tempFilesExtracted(tempFileNames);

        if (tmpProc_.exitCode() == 0)
          watcher_->addPath(tempFileNames.at(0));
      }
      return;
    }

    if (is7z_) {
      args << "-aos"; // skip extraction of existing files
      if (encrypted_ )
        args << "-p" + pswrd_;
      args << "x" << fileArgs_ << "-o" + arqiverDir_ << "-y" << realPaths;
      emit processStarting();
      tmpProc_.setStandardOutputFile(QProcess::nullDevice());
      tmpProc_.start("7z", args);
      if (tmpProc_.waitForStarted()) {
        while (!tmpProc_.waitForFinished(500))
          QCoreApplication::processEvents();
      }
      if (tmpProc_.exitCode() != 0)
        pswrd_ = QString();
    }
    else {
      /* bsdtar */
      bool noDir = true;
      for (const QString &str : paths) {
        if (isDir(str)) {
          noDir = false;
          break;
        }
      }
      // "--fast-read" interferes with extraction of directories
      if (noDir)
        args << "-x" << "--no-same-owner" << "--fast-read" << "-k" << fileArgs_;
      else
        args << "-x" << "--no-same-owner" << "-k" << fileArgs_;

      /* If a file comes after its containing folder in the command line,
         bsdtar doesn't extract the folder. So, we sort the list and read it inversely. */
      realPaths.sort();
      int N = realPaths.length();
      for (int i = 0; i < N; i++)
        args << "--include" << escapeSpecialChars(realPaths[N - 1 - i]);

      args << "-C" << arqiverDir_;
      emit processStarting();
      tmpProc_.setStandardOutputFile(QProcess::nullDevice());
      tmpProc_.start(tarCmnd_, args);
      if (tmpProc_.waitForStarted()) {
        while (!tmpProc_.waitForFinished(500))
          QCoreApplication::processEvents();
      }
    }

    emit processFinished(tmpProc_.exitCode() == 0, QString());
    emit tempFilesExtracted(tempFileNames);
    if (tmpProc_.exitCode() != 0) return;
  }
  else { // all paths are already extracted
    emit processFinished(true, QString());
    emit tempFilesExtracted(tempFileNames);
  }

  // files may be edited also by drag-and-drop
  for (const QString &str : std::as_const(tempFileNames)) {
    QFileInfo info(str);
    if (!info.isDir()) {
      const QString finalTarget = info.canonicalFilePath();
      if (finalTarget == str || finalTarget.startsWith(arqiverDir_ + QLatin1Char('/'))) {
        watcher_->addPath(finalTarget);
        if (!is7z_)
          modTimes_.insert(finalTarget, info.lastModified());
      }
    }
  }
}

void Backend::parseLines(QStringList& lines) {
  static bool hasSingleRoot = false;
  if (contents_.isEmpty()) {
    hasSingleRoot = true;
    archiveSingleRoot_ = QString(); // if existing, may mean a parent dir or single file
  }
  if (is7z_) {
    static int attrIndex = 0;
    static int cSizeIndex = 0; // end of the compressed size column
    static int nameIndex = 0;
    if (contents_.isEmpty()) {
      attrIndex = cSizeIndex = nameIndex = 0;
    }
    if (starting7z_) {
      /* ignore all p7zip header info */
      while (starting7z_ && !lines.isEmpty()) {
        if (lines.at(0) == "--")
          starting7z_ = false; // found the end of the headers
        lines.removeAt(0);
      }
    }
    for (int i = 0; i < lines.length(); i++) {
      if (lines.at(i).simplified().isEmpty() || lines.at(i).startsWith("----") || lines.at(i).startsWith(" = "))
        continue;
      if (listing_) {
        QString file;
        QStringList info = lines.at(i).split(" ",Qt::SkipEmptyParts);
        if (info.size() < 2) continue; // invalid line
        // Format: [Date, Time, Attr, Size, Compressed, Name]
        if (info.size() >= 6 && info.at(2) == "Attr") { // header
          attrIndex = lines.at(i).indexOf(info.at(2));
          cSizeIndex = lines.at(i).indexOf(info.at(4)) + info.at(4).size();
          nameIndex = lines.at(i).indexOf(info.at(5));
          continue;
        }
        const int lineSize = lines.at(i).size();
        if (attrIndex <= 0 || cSizeIndex <= 0 || nameIndex < 3
            || attrIndex >= lineSize || cSizeIndex >= lineSize || nameIndex >= lineSize) {
          continue; // the header should be read first
        }
        /* we suppose that the row starts either with Date or with Attr (Date and Time are empty) */
        QString attrStr = lines.at(i).mid(attrIndex - 1, 5); // the Attr column has 5 characters (like "....A")
        if (!attrStr.contains("."))
          continue; // a row that isn't related to a file
        bool hasCSize = !lines.at(i).at(cSizeIndex - 1).isSpace();
        if (info.size() < 5) {
          if (!info.at(0).contains(".")) continue; // should start with Attr (no Date and Time)
          file = lines.at(i).right(lineSize - nameIndex);
          if (file.isEmpty()) continue;
          if (info.size() == 2) { // only Attr and name (as with "application/x-bzip")
            if (!contents_.isEmpty()) continue; // invalid line
            archiveSingleRoot_ = file.section('/', 0, 0);
            QString s, cs;
            if (i < lines.length() - 2
                && lines.at(i+1).startsWith("---") // next line is end of table
                && attrIndex + 5 < lines.at(i+2).size()
                && lines.at(i+2).left(attrIndex + 6).simplified().isEmpty()) { // no Attr and nothing before it
              QStringList infoNext = lines.at(i+2).split(" ",Qt::SkipEmptyParts);
              // Format of infoNext: [Size(?), Compressed, Number(=1), "files"]
              if (!infoNext.isEmpty()) {
                if (cSizeIndex < lines.at(i+2).size() && !lines.at(i+2).at(cSizeIndex - 1).isSpace()) {
                  if (infoNext.size() == 3) {
                    cs = infoNext.at(0);
                    s = QString::number(0); // size is missing
                  }
                  else if (infoNext.size() == 4)
                    cs = infoNext.at(1);
                }
                if (s.isEmpty()) s = infoNext.at(0);
              }
            }
            if (s.isEmpty()) s = QString::number(0);
            if (cs.isEmpty()) cs = QString::number(0);
            contents_.insert(file, QStringList() << attrStr << s << cs);
            return;
          }
          contents_.insert(file,
                           QStringList() << attrStr << info.at(1)
                                         << (hasCSize ? info.at(2) : QString::number(0)));
        }
        else {
          file = lines.at(i).right(lineSize - nameIndex);
          if (file.isEmpty()) continue;
          if (info.at(0).contains(".")) { // starts with Attr (no Date and Time)
            contents_.insert(file,
                             QStringList() << attrStr << info.at(1)
                                           << (hasCSize ? info.at(2) : QString::number(0)));
          }
          else {
            contents_.insert(file,
                             QStringList() << attrStr << info.at(3)
                                           << (hasCSize ? info.at(4) : QString::number(0)));
          }
        }
        if (hasSingleRoot) {
          if (archiveSingleRoot_.isEmpty()) {
            archiveSingleRoot_ = file.section('/', 0, 0);
            if (archiveSingleRoot_.isEmpty())
              hasSingleRoot = false;
          }
          else if (archiveSingleRoot_ != file.section('/', 0, 0)) {
            hasSingleRoot = false;
            archiveSingleRoot_ = QString();
          }
        }
      }
    }
    return;
  }
  for (int i = 0; i < lines.length(); i++) {
    if (isGzip_) {
      // lines[i] is:                 <compressed_size>                   <uncompressed_size> -33.3% /x/y/z}
      QStringList info = lines.at(i).split(" ",Qt::SkipEmptyParts);
      if (contents_.isEmpty() && info.size() >= 4) {
        int indx = lines.at(i).indexOf("% ");
        if (indx > -1) {
          QString file = lines.at(i).mid(indx + 2).section('/', -1);
          if (filepath_.endsWith(".svgz") && file.endsWith(".svgz")) {
            /* WARNING: gzip lists the file as svgz again */
            file.remove(QRegularExpression("svgz$"));
            file += "svg";
          }
          archiveSingleRoot_ = file.section('/', 0, 0);
          contents_.insert(file,
                           QStringList() << "-rw-r--r--" << info.at(1) << QString()); // [perms, size, linkto]
        }
      }
      return;
    }
    int indx;
    QRegularExpressionMatch match;
    if (getMimeType(filepath_) == "application/zip") {
      static const QRegularExpression badZipExp("^\\s*x\\s+");
      indx = lines.at(i).indexOf(badZipExp, 0 , &match);
      if (indx == 0) {
        /* ZIP archives may not have all the extra information - just file names */
        QString file = lines.at(i).right(lines.at(i).length() - match.capturedLength());
        QString perms;
        if (file.endsWith("/")) {
          perms = "d";
          file.chop(1);
        }
        if (file.isEmpty()) continue; // impossible
        if (file.contains(startBslashExp))
          startBackslash_ = true;
        if (hasSingleRoot) {
          if (archiveSingleRoot_.isEmpty()) {
            archiveSingleRoot_ = file.section('/', 0, 0);
            if (archiveSingleRoot_.isEmpty())
              hasSingleRoot = false;
          }
          else if (archiveSingleRoot_ != file.section('/', 0, 0)) {
            hasSingleRoot = false;
            archiveSingleRoot_ = QString();
          }
        }
        contents_.insert (file, QStringList() << perms << "-1" << QString()); // [perms, size, linkto ]
        continue;
      }
    }
    // here, lines[i] is like: -rw-r--r--  0 USER GROUP  32616 Oct  4  2018 PATH
    static const QRegularExpression nonPathExp("(\\S+\\s+){7}\\S+ ");
    indx = lines.at(i).indexOf(nonPathExp, 0 , &match);
    if (indx != 0 || match.capturedLength() == lines.at(i).length())
      continue; // invalid line
    QStringList info;
    info << lines.at(i).left(match.capturedLength()).split(" ",Qt::SkipEmptyParts);
    info << lines.at(i).right(lines.at(i).length() - match.capturedLength());
    // here, info is like ("-rw-r--r--", "1", "0", "0", "645", "Feb", "5", "2016", "x/y -> /a/b")
    QString file = info.at(8);
    if (file.endsWith("/"))
      file.chop(1);
    if (file.isEmpty()) // possible in rare cases (with "application/x-archive", for example)
      continue;
    QString linkto;
    /* see if this file has the "link to" or "->"  notation */
    if (file.contains(" -> ")) {
      linkto = file.section(" -> ", 1, -1);
      file = file.section(" -> ", 0, 0);
    }
    else if (file.contains(" link to ")) {
      /* alternate form of a link within a tar archive (not reflected in perms) */
      linkto = file.section(" link to ", 1, -1);
      file = file.section(" link to ", 0, 0);
      if (info.at(0).startsWith("-"))
        info[0].replace(0, 1, "l");
    }
    if (file.contains(startBslashExp))
      startBackslash_ = true;
    if (hasSingleRoot) {
      if (archiveSingleRoot_.isEmpty()) {
          archiveSingleRoot_ = file.section('/', 0, 0);
          if (archiveSingleRoot_.isEmpty())
            hasSingleRoot = false;
      }
      else if (archiveSingleRoot_ != file.section('/', 0, 0)) {
          hasSingleRoot = false;
          archiveSingleRoot_ = QString();
      }
    }
    contents_.insert(file, QStringList() << info.at(0) << info.at(4) << linkto); // [perms, size, linkto ]
  }
}

void Backend::startList(bool withPassword) {
  contents_.clear();
  startBackslash_ = false;
  keyArgs_.clear();
  listing_ = true;
  if (isGzip_) {
    keyArgs_ << "-l";
    proc_.start("gzip", QStringList() << "-l" << filepath_);
  }
  else if (is7z_) {
    QStringList args;
    args << "-p" + (withPassword ? pswrd_ : QString()); // needed for encrypted lists
    if (!encryptionQueried_)
      args << "-slt"; // query encryption instead of listing
    args << "l";
    starting7z_ = true;
    keyArgs_ << "l";
    proc_.start("7z", QStringList() << args << fileArgs_);
  }
  else {
    QStringList args;
    args << "-tv";
    keyArgs_ << "-tv";
    proc_.start(tarCmnd_, QStringList() << args << fileArgs_);
  }
}

void Backend::procFinished(int retcode, QProcess::ExitStatus) {
  if (isKilled_) {
    isKilled_ = false;
    if (keyArgs_.contains("l") || keyArgs_.contains("-tv") || keyArgs_.contains("-l")) { // listing
      /* reset most variables (the rest will be reset with the next loading) */
      listing_ = false;
      isGzip_ = is7z_ = false;
      starting7z_ = encryptionQueried_ = encrypted_ = encryptedList_ = false;
      keyArgs_.clear();
      contents_.clear();
      startBackslash_ = false;
      insertQueue_.clear();
      encryptedPaths_.clear();
      pswrd_ = archiveSingleRoot_ = result_ = data_ = QString();

      emit processFinished(false, tr("Could not read archive"));
      return;
    }
  }

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
      proc_.start("7z", QStringList() << args << fileArgs_);
    }
    return;
  }

  /* NOTE: processFinished() should be emitted once, in the end */
  processData();
  listing_ = false;
  if (is7z_) {
    starting7z_ = false;
    if (keyArgs_.contains("l")) { // listing
      emit loadingFinished();
      if (retcode != 0) {
        contents_.clear();
        startBackslash_ = false;
        result_ = tr("Could not read archive");
      }
      else if (result_.isEmpty()) {
        result_ = tr("Archive Loaded");
        emit loadingSuccessful();
      }
      emit processFinished(retcode == 0, result_);
      result_.clear();
    }
    else if (keyArgs_.contains("a") || keyArgs_.contains("d")) { // addition/removal
      result_ = tr("Modification Finished");
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
        QStringList args = proc_.arguments();
        for (int i = 0; i < args.length(); i++) {
          if (args.at(i).startsWith("-o")) //just extracted to a dir - open it now
            QProcess::startDetached("xdg-open \"" + args.at(i).section("-o", 1, -1) + "\"");
        }
      }*/
      emit extractionFinished();
      if (retcode == 0) {
        emit extractionSuccessful();
        result_ = tr("Extraction Finished");
      }
      else if (result_.isEmpty())
        result_ = tr("Extraction Failed");
      emit processFinished(retcode == 0, result_);
      result_.clear();
    }
    return;
  }
  if (keyArgs_.contains("-tv") || keyArgs_.contains("-l")) { // listing
    if (retcode != 0) {
      contents_.clear();
      startBackslash_ = false;
      result_ = tr("Could not read archive");
    }
    else if (result_.isEmpty()) {
      emit loadingFinished();
      if (retcode == 0) {
        result_ = tr("Archive Loaded");
        emit loadingSuccessful();
      }
      if (isGzip_)
        emit archivingSuccessful(); // because the created archive is just loaded (-> startAdd)
    }
    emit processFinished(retcode == 0, result_);
    result_.clear();
  }
  else
  {
    bool updateList = true;
    if (keyArgs_.contains("-x")) { // extraction
      updateList = false;
      emit extractionFinished();
      if (retcode == 0)
      {
        result_ = tr("Extraction Finished");
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
      else if (result_.isEmpty())
        result_ = tr("Extraction Failed");
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

      result_ = tr("Modification Finished");
      if (insertQueue_.isEmpty())
        emit archivingSuccessful();
      else {
        updateList = false;
        QTimer::singleShot(0, this, &Backend::startInsertFromQueue);
      }
    }
    if (updateList)
      startList();
    else {
      emit processFinished(retcode == 0, result_);
      result_.clear();
    }
  }
}

void Backend::processData() {
  if (is7z_ && !encryptionQueried_) {
    if (!encryptedList_) {
      QString read = proc_.readAllStandardOutput();
      if (read.contains("\nERROR: ") && read.contains("encrypted")) { // ERROR: FILE_PATH : Can not open encrypted archive. Wrong password?
        encryptedList_ = encrypted_ = true;
      }
      else {
        const QStringList& items = read.split("\n\n", Qt::SkipEmptyParts);
        for (const QString& thisItem : items) {
          if (thisItem.contains("\nEncrypted = +")) {
            /* the archive has an encrypted file but its header isn't encrypted */
            QStringList lines = thisItem.split("\n", Qt::SkipEmptyParts);
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
  QString read = data_ + proc_.readAllStandardOutput();
  if (read.endsWith("\n"))
    data_.clear();
  else {
    data_ = read.section("\n", -1);
    read = read.section("\n", 0, -2);
  }

  /* NOTE: Because 7x doesn't escape newlines in file names, the archives
           that contain such files can't be processed correctly. */
  QStringList lines = read.split("\n", Qt::SkipEmptyParts);

  /* Gzip doesn't escape newlines either but that can be worked around */
  if (isGzip_ && lines.size() >= 2) {
    lines.removeFirst();
    QString joined = lines.join('\n');
    lines.clear();
    lines << joined;
  }

  if (listing_)
    parseLines(lines);
  if (is7z_) {
    if (read.contains("\nERROR")) { // ERROR: Data Error in encrypted file. Wrong password?
      /* while extracting the archive, another file in it had another password */
      pswrd_ = QString();
    }
    emit progressUpdate(-1, QString());
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
                  .arg(proc_.program()));
}

bool Backend::isWorking() { // used only with DND
  return (proc_.state() == QProcess::Running
          || tmpProc_.state() == QProcess::Running);
}

void Backend::killProc() {
  if (proc_.state() == QProcess::Running) {
    isKilled_ = true;
    proc_.kill();
  }
  else if (tmpProc_.state() == QProcess::Running) { // tmpProc_ starts after proc_ is finished
    tmpProc_.kill();
  }
}

}
