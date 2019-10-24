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

#include "mainWin.h"
#include "ui_mainWin.h"
#include "ui_about.h"
#include "svgicons.h"
#include "pref.h"

#include <QMessageBox>
#include <QFileDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QTimer>
#include <QRegularExpression>
//#include <QScreen>
#include <QPixmapCache>
#include <QClipboard>

#include <unistd.h> // getuid

namespace Arqiver {

static const QRegularExpression archivingExt("\\.(tar\\.gz|tar\\.xz|tar\\.bz|tar\\.bz2|tar\\.lzma|tar|zip|tgz|txz|tbz|tbz2|tlz|cpio|ar|7z|gz)$");

mainWin::mainWin() : QMainWindow(), ui(new Ui::mainWin) {
  ui->setupUi(this);

  lastPath_ = QDir::homePath();
  updateTree_ = true; // will be set to false when extracting (or viewing)
  expandAll_ = false;
  close_ = false;
  processIsRunning_ = false;
  filterTimer_ = nullptr;
  reapplyFilter_ = true;

  ui->tree_contents->installEventFilter(this);

  if (getuid() == 0)
    setWindowTitle("Arqiver (" + tr("Root") + ")");

  /* status bar */
  iconLabel_ = new QLabel();
  iconLabel_->setFixedSize(16, 16);
  iconLabel_->setFocusPolicy(Qt::NoFocus);
  textLabel_ = new Label();
  textLabel_->setFocusPolicy(Qt::NoFocus);
  statusProgress_ = new QProgressBar();
  statusProgress_->setTextVisible(false);
  statusProgress_->setMaximumHeight(qMax(QFontMetrics(font()).height(), 16));
  statusProgress_->setFocusPolicy(Qt::NoFocus);
  ui->statusbar->addWidget(iconLabel_);
  ui->statusbar->addWidget(statusProgress_);
  ui->statusbar->addPermanentWidget(textLabel_);
  iconLabel_->setVisible(false);
  statusProgress_->setVisible(false);

  /* icons */
  setWindowIcon(QIcon::fromTheme("utilities-file-archiver"));
  ui->actionNew->setIcon(symbolicIcon::icon(":icons/document-new.svg"));
  ui->actionOpen->setIcon(symbolicIcon::icon(":icons/document-open.svg"));
  ui->actionQuit->setIcon(symbolicIcon::icon(":icons/application-exit.svg"));
  ui->actionAddFile->setIcon(symbolicIcon::icon(":icons/archive-insert.svg"));
  ui->actionAddDir->setIcon(symbolicIcon::icon(":icons/archive-insert-directory.svg"));
  ui->actionRemoveFile->setIcon(symbolicIcon::icon(":icons/archive-remove.svg"));
  ui->actionExtractAll->setIcon(symbolicIcon::icon(":icons/archive-extract.svg"));
  ui->actionExtractSel->setIcon(symbolicIcon::icon(":icons/edit-select-all.svg"));
  ui->actionPassword->setIcon(symbolicIcon::icon(":icons/locked.svg"));
  ui->actionExpand->setIcon(symbolicIcon::icon(":icons/expand.svg"));
  ui->actionCollapse->setIcon(symbolicIcon::icon(":icons/collapse.svg"));
  ui->actionAbout->setIcon(symbolicIcon::icon(":icons/help-about.svg"));
  ui->actionCopy->setIcon(symbolicIcon::icon(":icons/edit-copy.svg"));
  ui->actionPref->setIcon(symbolicIcon::icon(":icons/preferences-system.svg"));

  ui->label->setVisible(false);
  ui->label_archive->setVisible(false);

  ui->actionAddFile->setEnabled(false);
  ui->actionRemoveFile->setEnabled(false);
  ui->actionExtractAll->setEnabled(false);
  ui->actionAddDir->setEnabled(false);
  ui->actionExtractSel->setEnabled(false);
  ui->actionPassword->setEnabled(false);

  BACKEND = new Backend(this);
  connect(BACKEND, &Backend::processStarting, this, &mainWin::procStarting);
  connect(BACKEND, &Backend::processFinished, this, &mainWin::procFinished);
  connect(BACKEND, &Backend::progressUpdate, this, &mainWin::procUpdate);
  connect(BACKEND, &Backend::encryptedList, this, &mainWin::openEncryptedList);
  connect(BACKEND, &Backend::errorMsg, this, [this](const QString& msg) {
    QMessageBox::critical(this, tr("Error"), msg);
  });

  QWidget *spacer = new QWidget(this);
  spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  ui->toolBar->insertWidget(ui->actionAddFile, spacer);

  connect(ui->actionNew, &QAction::triggered, this, &mainWin::newArchive);
  connect(ui->actionOpen, &QAction::triggered, this, &mainWin::openArchive);
  connect(ui->actionQuit, &QAction::triggered, this, &mainWin::close);
  connect(ui->actionAddFile, &QAction::triggered, this, &mainWin::addFiles);
  connect(ui->actionRemoveFile, &QAction::triggered, this, &mainWin::removeFiles);
  connect(ui->actionExtractAll, &QAction::triggered, this, &mainWin::extractFiles);
  connect(ui->actionExtractSel, &QAction::triggered, this, &mainWin::extractSelection);
  connect(ui->actionAddDir, &QAction::triggered, this, &mainWin::addDirs);
  connect(ui->actionCopy, &QAction::triggered, [this] {
    if (QTreeWidgetItem *cur = ui->tree_contents->currentItem())
      QApplication::clipboard()->setText(cur->whatsThis(0));
  });
  connect(ui->actionPassword, &QAction::triggered, [this] {pswrdDialog(true);});
  connect(ui->tree_contents, &QTreeWidget::itemDoubleClicked, this, &mainWin::viewFile);
  connect(ui->tree_contents, &TreeWidget::enterPressed, this, &mainWin::expandOrView);
  connect(ui->tree_contents, &QTreeWidget::itemSelectionChanged, this, &mainWin::selectionChanged);
  connect(ui->tree_contents, &TreeWidget::dragStarted, this, &mainWin::extractSingleFile);
  connect(ui->tree_contents, &QWidget::customContextMenuRequested, this, &mainWin::listContextMenu);

  connect(ui->actionExpand, &QAction::triggered, [this] {ui->tree_contents->expandAll();});
  connect(ui->actionCollapse, &QAction::triggered, [this] {ui->tree_contents->collapseAll();});

  connect(ui->lineEdit, &QLineEdit::textChanged, this, &mainWin::filter);

  connect(ui->actionPref, &QAction::triggered, this, &mainWin::prefDialog);

  connect(ui->actionAbout, &QAction::triggered, this, &mainWin::aboutDialog);

  /* the labels and column sizes of the header (the 4th hidden column will
     be used for putting directory items first and will save the lock info) */
  ui->tree_contents->setHeaderLabels(QStringList() << tr("File") << tr("MimeType") << tr("Size") << QString());
  ui->tree_contents->header()->setSectionHidden(3, true);
  ui->tree_contents->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  QTimer::singleShot(0, this, [this]() {
    ui->tree_contents->resizeColumnToContents(2);
  });
  QTimer::singleShot(0, this, [this]() {
    ui->tree_contents->resizeColumnToContents(1);
  });

  /* support file dropping into the window */
  setAcceptDrops(true);

  /* apply the configuration */
  config_.readConfig();
  if (config_.getRemSize()) {
    resize(config_.getWinSize());
    if (config_.getIsMaxed())
      setWindowState(Qt::WindowMaximized);
  }
  else {
    QSize startSize = config_.getStartSize();
    /*QSize ag;
    if (QScreen *pScreen = QApplication::primaryScreen()) // the window isn't shown yet
      ag = pScreen->availableVirtualGeometry().size();
    if (!ag.isEmpty() && (startSize.width() > ag.width() || startSize.height() > ag.height())) {
      startSize = startSize.boundedTo(ag);
      config_.setStartSize(startSize);
    }
    else */if (startSize.isEmpty()) {
      startSize = QSize(600, 500);
      config_.setStartSize(startSize);
    }
    resize(startSize);
  }

  lastFilter_ = config_.getLastFilter();
  if (filterToExtension(lastFilter_).isEmpty()) // validate the filter
    lastFilter_.clear();

  int icnSize = config_.getIconSize();
  ui->tree_contents->setIconSize(QSize(icnSize, icnSize));
}

mainWin::~mainWin() {
  config_.writeConfig();
  if (filterTimer_) {
    disconnect(filterTimer_, &QTimer::timeout, this, &mainWin::reallyApplyFilter);
    filterTimer_->stop();
    delete filterTimer_;
  }
}

bool mainWin::eventFilter(QObject *watched, QEvent *event) {
  if (watched == ui->tree_contents && event->type() == QEvent::KeyPress) {
    /* when a text is typed inside the tree, type it inside the filter line-edit */
    if (QKeyEvent *ke = static_cast<QKeyEvent*>(event)) {
      if (ke->key() != Qt::Key_Return && ke->key() != Qt::Key_Enter
          && ke->key() != Qt::Key_Up && ke->key() != Qt::Key_Down
          && ke->key() != Qt::Key_Home && ke->key() != Qt::Key_End
          && ke->key() != Qt::Key_PageUp && ke->key() != Qt::Key_PageDown) {
        ui->lineEdit->pressKey(ke);
        return false;
      }
    }
  }
  return QMainWindow::eventFilter(watched, event);
}

void mainWin::filter(const QString&/*text*/) {
  if (filterTimer_ == nullptr) {
    filterTimer_ = new QTimer();
    filterTimer_->setSingleShot(true);
    connect(filterTimer_, &QTimer::timeout, this, &mainWin::reallyApplyFilter);
  }
  filterTimer_->start(350);
}

void mainWin::reallyApplyFilter() {
  const QString filter = ui->lineEdit->text();
  QTreeWidgetItemIterator it(ui->tree_contents);
  if (filter.isEmpty()) {
    while (*it) {
      (*it)->setHidden(false);
      ++it;
    }
    return;
  }
  while (*it) {
    if (!(*it)->text(1).isEmpty() && !(*it)->text(0).contains(filter, Qt::CaseInsensitive))
      (*it)->setHidden(true);
    else
      (*it)->setHidden(false);
    ++it;
  }
  /* hide all childless directories to make filtering practical */
  QTreeWidgetItemIterator it1(ui->tree_contents);
  while (*it1) {
    hideChildlessDir((*it1));
    ++it1;
  }
}

void mainWin::hideChildlessDir(QTreeWidgetItem *item) {
  if (item->isHidden() || !item->text(1).isEmpty())
    return;
  int N = item->childCount();
  for (int i = 0; i < N; ++i) {
    QTreeWidgetItem *child = item->child(i);
    if (child->isHidden()) continue;
    hideChildlessDir(child);
    if (!child->isHidden()) {
      child->setHidden(false);
      return;
    }
  }
  item->setHidden(true);
}

void mainWin::closeEvent(QCloseEvent *event) {
  if(processIsRunning_)
    event->ignore();
  else {
    if (config_.getRemSize() && windowState() == Qt::WindowNoState)
      config_.setWinSize(size());
    config_.setLastFilter(lastFilter_);
    event->accept();
  }
}

void mainWin::changeEvent(QEvent *event) {
  if (config_.getRemSize() && event->type() == QEvent::WindowStateChange) {
    if (windowState() == Qt::WindowFullScreen) // impossible
      config_.setIsMaxed (true);
    else if (windowState() == (Qt::WindowFullScreen ^ Qt::WindowMaximized))  // impossible
      config_.setIsMaxed (true);
    else
      config_.setIsMaxed (windowState() == Qt::WindowMaximized);
  }
  QWidget::changeEvent (event);
}

void mainWin::loadArguments(const QStringList& args) {
  /* KDE needs all events to be processed ; otherwise, if a dialog
     is shown before the main window, the application won't exit
     when the main window is closed. This should be a bug in KDE.
     As a workaround, we show the window only when no dialog is
     going to be shown before it. */
  QTimer::singleShot(0, this, [this, args]() {
    int action = -1;
    /*
      0: auto extracting   -> arqiver --ax Archive(s)
      1: auto archiving    -> arqiver --aa Archive Files
      2: simple extracting -> arqiver --sx Archive
      3: simple archiving  -> arqiver --sa Files
    */
    QStringList files;
    for (int i = 0; i < args.length(); i++) {
      if (args[i].startsWith("--")) {
        if (action >= 0) break;
        else if (args[i]=="--ax") {
          action = 0; continue;
        }
        else if (args[i]=="--aa") {
          action = 1; continue;
        }
        else if (args[i]=="--sx") {
          action = 2; continue;
        }
        else if (args[i]=="--sa") {
          action = 3; continue;
        }
      }
      else {
        files << args[i];
      }
    }

    if (action != 3) // no dialog is shown before the main window
      show();

    if (files.isEmpty()) return;
    files.removeDuplicates();
    if (!files.at(0).isEmpty())
      lastPath_ = files.at(0).section("/", 0, -2);

    textLabel_->setText(tr("Opening Archive..."));
    if (action == 0) {
      axFileList_ = files;
      /* here, we don't wait for a successful operation because all files
        should be processed and the window should be closed at the end */
      connect(BACKEND, &Backend::loadingFinished, this, &mainWin::autoextractFiles);
      connect(BACKEND, &Backend::extractionFinished, this, &mainWin::nextAutoExtraction);
      BACKEND->loadFile(axFileList_.first());
      axFileList_.removeFirst();
    }
    else if (action == 1) {
      aaFileList_ = files;
      aaFileList_.removeFirst();
      connect(BACKEND, &Backend::loadingSuccessful, this, &mainWin::autoArchiveFiles);
      connect(BACKEND, &Backend::archivingSuccessful, [this] {close_ = true;});
      BACKEND->loadFile(files[0]);
    }
    else if (action == 2) {
      connect(BACKEND, &Backend::loadingSuccessful, this, &mainWin::simpleExtractFiles);
      connect(BACKEND, &Backend::extractionSuccessful, [this] {close_ = true;});
      BACKEND->loadFile(files[0]);
    }
    else if (action == 3) {
      saFileList_ = files;
      connect(BACKEND, &Backend::loadingSuccessful, this, &mainWin::simpleArchivetFiles);
      connect(BACKEND, &Backend::archivingSuccessful, [this] {close_ = true;});
      newArchive();
    }
    else {
      expandAll_ = true;
      BACKEND->loadFile(files[0]);
    }
  });
}

QHash<QString, QTreeWidgetItem*> mainWin::cleanTree(const QStringList& list) {
  QHash<QString, QTreeWidgetItem*> items;
  if (ui->tree_contents->topLevelItemCount() == 0)
    return items;
  if (list.isEmpty()) {
    ui->tree_contents->clear();
    return items;
  }

  QTreeWidgetItemIterator it(ui->tree_contents);
  while (*it) {
    const QString path = (*it)->whatsThis(0);
    if (!list.contains(path)
        || (*it)->data(2, Qt::UserRole).toString() != BACKEND->sizeString(path) // size change
        /* also consider the possibility of lock state change */
        || (BACKEND->is7z()
            && (((*it)->data(3, Qt::UserRole).toString() == "lock" // see updateTree()
                 && !BACKEND->isEncryptedPath(path))
                || ((*it)->data(3, Qt::UserRole).toString().isEmpty()
                    && BACKEND->isEncryptedPath(path))))) {
      qDeleteAll((*it)->takeChildren());
      delete *it;
    }
    else {
      items.insert(path, *it);
      ++it;
    }
  }
  return items;
}

QString mainWin::allArchivingTypes() {
  static const QString allTypes = tr("All Types %1").arg("(*.tar.gz *.tar.xz *.tar.bz *.tar.bz2 *.tar.lzma *.tar *.zip *.tgz *.txz *.tbz *.tbz2 *.tlz *.cpio *.ar *.7z *.gz)");
  return allTypes;
}

QString mainWin::archivingTypes() {
  static QString fileTypes;
  if (fileTypes.isEmpty()) {
    QStringList types;
    types << allArchivingTypes();
    QStringList tmp;
    tmp << tr("Uncompressed Archive (*.tar)");
    tmp << tr("GZip Compressed Archive (*.tar.gz *.tgz)");
    //tmp << tr("BZip Compressed Archive (*.tar.bz *.tbz)");
    tmp << tr("BZip2 Compressed Archive (*.tar.bz2 *.tbz2 *.tbz)");
    tmp << tr("LMZA Compressed Archive (*.tar.lzma *.tlz)");
    tmp << tr("XZ Compressed Archive (*.tar.xz *.txz)");
    tmp << tr("CPIO Archive (*.cpio)");
    //types << tr("PAX Archive (*.pax)");
    tmp << tr("AR Archive (*.ar)");
    //tmp << tr("SHAR Archive (*.shar)");
    tmp << tr("Zip Archive (*.zip)");
    tmp << tr("Gzip Archive (*.gz)");
    tmp << tr("7-Zip Archive (*.7z)");
    tmp.sort();
    types << tmp;
    fileTypes = types.join(";;");
  }
  return fileTypes;
}

QHash<QString, QString> mainWin::supportedMimeTypes() {
  static QHash<QString, QString> supported;
  if (supported.isEmpty()) {
    supported.insert ("application/x-tar", tr("Uncompressed Archive (*.tar)"));
    supported.insert ("application/x-compressed-tar", tr("GZip Compressed Archive (*.tar.gz *.tgz)"));
    //supported.insert ("application/x-bzip-compressed-tar", tr("BZip Compressed Archive (*.tar.bz *.tbz)"));
    supported.insert ("application/x-bzip-compressed-tar", tr("BZip2 Compressed Archive (*.tar.bz2 *.tbz2 *.tbz)"));
    supported.insert ("application/x-bzip", tr("BZip2 Archive (*.bz2)"));
    supported.insert ("application/x-xz-compressed-tar", tr("XZ Compressed Archive (*.tar.xz *.txz)"));
    supported.insert ("application/x-lzma-compressed-tar", tr("LMZA Compressed Archive (*.tar.lzma *.tlz)"));
    supported.insert ("application/x-cpio", tr("CPIO Archive (*.cpio)"));
    //supported.insert ("?", tr("PAX Archive (*.pax)"));
    supported.insert ("application/x-archive", tr("AR Archive (*.ar)"));
    //supported.insert ("application/x-shar", tr("SHAR Archive (*.shar)"));
    supported.insert ("application/zip", tr("Zip Archive (*.zip)"));
    supported.insert ("application/x-7z-compressed", tr("7-Zip Archive (*.7z)"));
    supported.insert ("application/gzip", tr("Gzip Archive (*.gz)"));
    supported.insert ("application/x-gzpdf", tr("Gzip Compressed PDF Document (*.pdf.gz)"));
    supported.insert ("image/svg+xml-compressed", tr("READ-ONLY: Compressed SVG Image (*.svgz)"));
    supported.insert ("application/x-cd-image", tr("READ-ONLY: ISO Image (*.iso *.img)"));
    supported.insert ("application/x-raw-disk-image", tr("READ-ONLY: ISO Image (*.iso *.img)"));
    supported.insert ("application/x-xar", tr("READ-ONLY: XAR Archive (*.xar)"));
    supported.insert ("application/x-java-archive", tr("READ-ONLY: Java Archive (*.jar)"));
    supported.insert ("application/vnd.debian.binary-package", tr("READ-ONLY: Debian Package (*.deb)"));
    supported.insert ("application/x-rpm", tr("READ-ONLY: RedHat Package (*.rpm)"));
    supported.insert ("application/x-source-rpm", tr("READ-ONLY: RedHat Package (*.rpm)"));
  }
  return supported;
}

QString mainWin::openingTypes() {
  static QString fileTypes;
  if (fileTypes.isEmpty()) {
    QStringList types;
    types << tr("All Known Types %1").arg("(*.tar.gz *.tar.xz *.tar.bz *.tar.bz2 *.bz2 *.tar.lzma *.tar *.zip *.tgz *.txz *.tbz *.tbz2 *.tlz *.cpio *.ar *.7z *.gz *.svgz *.iso *.img *.xar *.jar *.deb *.rpm)");
    QStringList l = supportedMimeTypes().values();
    l.removeDuplicates();
    l.sort();
    types << l;
    types << tr("Show All Files (*)");
    fileTypes = types.join(";;");
  }
  return fileTypes;
}

QString mainWin::filterToExtension(const QString& filter) {
  if (filter.isEmpty()) return QString();

  QString f = filter;
  auto left = f.indexOf('(');
  if (left != -1) {
    ++left;
    auto right = f.indexOf(')', left);
    if (right == -1)
      right = f.length();
    f = f.mid(left, right - left);
  }
  else
    return QString();

  auto list = f.simplified().split(' ');
  if (!list.isEmpty()) {
    f = list.at(0);
    if (!f.isEmpty()) {
      f.remove("*");
    }
  }
  else return QString();

  /* validate the extension */
  if (!f.isEmpty()) {
    QRegularExpressionMatch match;
    int indx = f.indexOf(archivingExt, 0, &match);
    if (indx == 0 && indx + match.capturedLength() == f.length())
      return f;
  }

  return QString();
}

void mainWin::newArchive() {
  QString file;

  QString ext = (lastFilter_.isEmpty() ? ".tar.gz" : filterToExtension(lastFilter_));
  QString path;
  if (!saFileList_.isEmpty())
    path =  saFileList_.at(0);
  bool retry(true);
  while (retry) {
    QFileDialog dlg(this, tr("Create Archive"),
                    /* KDE's buggy file dialog needs a directory path here */
                    path.isEmpty() ? lastPath_ : path.section("/", 0, -2),
                    archivingTypes());
    if (!path.isEmpty()) { // add an appropriate extension
      path += ext;
      dlg.selectFile(path);
    }
    dlg.setAcceptMode(QFileDialog::AcceptSave);
    dlg.setFileMode(QFileDialog::AnyFile);
    dlg.selectNameFilter(lastFilter_);
    if (dlg.exec()) {
      QString f = dlg.selectedNameFilter();
      if (f != allArchivingTypes())
        lastFilter_ = f;
      file = dlg.selectedFiles().at(0);
    }
    else return; // with auto-archiving, the application will exit because its main window isn't shown
    if (file.isEmpty()) return;
    QRegularExpressionMatch match;
    int indx = file.lastIndexOf(archivingExt, -1, &match);
    if (indx > 0 && indx + match.capturedLength() == file.length())
      retry = false; // the input had an acceptable extension
    else {
      if (QFile::exists(file + ext)) {
        QMessageBox::StandardButton btn = QMessageBox::question(this,
                                                                tr("Question"),
                                                                tr("The following archive already exists:")
                                                                + QString("\n\n%1\n\n").arg(file)
                                                                + tr("Do you want to replace it?\n"));
        if (btn == QMessageBox::No)
          path = file;
        else {
          file += ext;
          retry = false;
        }
      }
      else {
        file += ext;
        retry = false;
      }
    }
  }

  /* we don't show the window with auto-archiving (see loadArguments) */
  if (!isVisible())
    show();

  lastPath_ = file.section("/", 0, -2);

  textLabel_->clear();
  BACKEND->loadFile(file);
}

void mainWin::openArchive() {
  QString file;
  QFileDialog dlg(this, tr("Open Archive"), lastPath_, openingTypes());
  dlg.setAcceptMode(QFileDialog::AcceptOpen);
  dlg.setFileMode(QFileDialog::ExistingFile);
  if (dlg.exec())
    file = dlg.selectedFiles().at(0);
  if (file.isEmpty()) return;
  lastPswrd_.clear();
  lastPath_ = file.section("/", 0, -2);
  textLabel_->setText(tr("Opening Archive..."));
  expandAll_ = true;
  BACKEND->loadFile(file);
}

void mainWin::dragEnterEvent(QDragEnterEvent *event) {
  if (event->source()) {
    // if the drag has originated in this window, ignore it
    // (needed when an archive contains archives, as with deb packages)
    event->ignore();
    return;
  }
  if (event->mimeData()->hasUrls()) {
    const QList<QUrl> urlList = event->mimeData()->urls();
    if (!urlList.isEmpty()) {
      QString file = urlList.at(0).toLocalFile();
      if (!file.isEmpty()) {
        QString mime = BACKEND->getMimeType(file);
        if (!supportedMimeTypes().contains(mime)) {
          event->ignore();
          return;
        }
      }
    }
    event->acceptProposedAction();
  }
  else event->ignore();
}

void mainWin::dropEvent(QDropEvent *event) {
  const QList<QUrl> urlList = event->mimeData()->urls();
  if (!urlList.isEmpty()) {
    QString file = urlList.at(0).toLocalFile();
    if (!file.isEmpty()) {
      lastPswrd_.clear();
      lastPath_ = file.section("/", 0, -2);
      textLabel_->setText(tr("Opening Archive..."));
      expandAll_ = true;
      BACKEND->loadFile(file);
      raise();
      activateWindow();
    }
  }
  event->acceptProposedAction();
}

void mainWin::addFiles() {
  QStringList files;
  if(BACKEND->isGzip()) { // accepts only one file
    QFileDialog dialog(this);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setWindowTitle(tr("Add to Archive"));
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setDirectory(lastPath_);
    if(dialog.exec()) {
      files = dialog.selectedFiles();
    }
  }
  else
    files = QFileDialog::getOpenFileNames(this, tr("Add to Archive"), lastPath_);
  if (files.isEmpty()) return;
  if (!files.at(0).isEmpty())
    lastPath_ = files.at(0).section("/", 0, -2);

  /* NOTE: If the archive has list encryption or a file already exists
           in the archive and is encrypted, the password will be needed.
           Otherwise, the password will be optional. */
  bool passWordSet(false);
  if (lastPswrd_.isEmpty() && BACKEND->hasEncryptedList()) {
    if (!pswrdDialog(true, true)) return;
    passWordSet = true;
  }
  int tc = ui->tree_contents->topLevelItemCount();
  for (int i = 0; i < tc; ++i) {
    QTreeWidgetItem *item = ui->tree_contents->topLevelItem(i);
    for (auto &file : qAsConst(files)) {
      if(file.section("/",-1) == item->whatsThis(0)) {
        BACKEND->removeSingleExtracted(item->whatsThis(0)); // the file will be replaced
        if (lastPswrd_.isEmpty() && subTreeIsEncrypted(item)) {
          if (!pswrdDialog()) return;
          passWordSet = true;
          break;
        }
      }
    }
  }
  if (lastPswrd_.isEmpty() && !passWordSet) // optional password
    BACKEND->setPswrd(QString());

  textLabel_->setText(tr("Adding Items..."));
  BACKEND->startAdd(files);
}

void mainWin::addDirs() { // only a single directory for now
  QString dir = QFileDialog::getExistingDirectory(this, tr("Add to Archive"), lastPath_ );
  if (dir.isEmpty()) return;
  lastPath_ = dir;

  /* as in addFiles() */
  bool passWordSet(false);
  if (lastPswrd_.isEmpty() && BACKEND->hasEncryptedList()) {
    if (!pswrdDialog(true, true)) return;
    passWordSet = true;
  }
  int tc = ui->tree_contents->topLevelItemCount();
  for (int i = 0; i < tc; ++i) {
    QTreeWidgetItem *item = ui->tree_contents->topLevelItem(i);
    if(dir.section("/",-1) == item->whatsThis(0)) {
      BACKEND->removeSingleExtracted(item->whatsThis(0)); // the directory will be replaced
      if (lastPswrd_.isEmpty() && subTreeIsEncrypted(item)) {
        if (!pswrdDialog()) return;
        passWordSet = true;
      }
      break;
    }
  }
  if (lastPswrd_.isEmpty() && !passWordSet)
    BACKEND->setPswrd(QString());

  textLabel_->setText(tr("Adding Items..."));
  BACKEND->startAdd(QStringList() << dir);
}

// Check if this item or any of its children is encrypted.
bool mainWin::subTreeIsEncrypted(QTreeWidgetItem *item) {
  if (BACKEND->isEncrypted() && BACKEND->getPswrd().isEmpty()) {
    if (BACKEND->isEncryptedPath(item->whatsThis(0)))
      return true;
    int N = item->childCount();
    if (N > 0) {
      for (int i = 0; i < N; ++i) {
        if (subTreeIsEncrypted(item->child(i)))
          return true;
      }
    }
  }
  return false;
}

void mainWin::removeFiles() {
  /* WARNING: 7z isn't self-consistent in file removal: sometimes it needs password,
              sometimes not. Moreover, it may remove files with a wrong password. */
  if (BACKEND->isEncrypted() && BACKEND->getPswrd().isEmpty() && !pswrdDialog())
    return;
  const QList<QTreeWidgetItem*> sel = ui->tree_contents->selectedItems();
  QStringList items;
  for (auto item : sel) {
    /*if (subTreeIsEncrypted(item)) {
      if (!pswrdDialog()) return;
    }*/
    items << item->whatsThis(0);
  }
  items.removeDuplicates();
  textLabel_->setText(tr("Removing Items..."));
  BACKEND->startRemove(items);
}

void mainWin::extractSingleFile(QTreeWidgetItem *it) {
  if (it->text(1).isEmpty()) return; // it's a directory item
  if (BACKEND->isEncrypted() && BACKEND->getPswrd().isEmpty()
      && BACKEND->isEncryptedPath(it->whatsThis(0))
      && !BACKEND->isSingleExtracted(it->whatsThis(0))
      && !pswrdDialog()) {
    return;
  }
  updateTree_ = false;
  reapplyFilter_ = false;
  textLabel_->setText(tr("Extracting..."));
  it->setData(0, Qt::UserRole, BACKEND->extractSingleFile(it->whatsThis(0)));
}

void mainWin::listContextMenu(const QPoint& p) {
  QModelIndex index = ui->tree_contents->indexAt(p);
  if (!index.isValid()) return;
  QTreeWidgetItem *item = ui->tree_contents->getItemFromIndex(index);
  bool isDir(item->text(1).isEmpty());

  QMenu menu;
  menu.addAction(ui->actionExtractSel);
  if (!isDir) {
    QAction *action = menu.addAction(tr("View Current Item"));
    connect(action, &QAction::triggered, action, [this, item] {viewFile(item);});
  }
  if(!isDir || !BACKEND->is7z())
    menu.addSeparator();
  menu.addAction(ui->actionCopy);
  menu.exec(ui->tree_contents->viewport()->mapToGlobal(p));
}

bool mainWin::pswrdDialog(bool listEncryptionBox, bool forceListEncryption) {
  QDialog *dialog = new QDialog(this);
  dialog->setWindowTitle(tr("Enter Password"));
  QGridLayout *grid = new QGridLayout;
  grid->setSpacing (5);
  grid->setContentsMargins (5, 5, 5, 5);

  QLineEdit *lineEdit = new QLineEdit();
  lineEdit->setMinimumWidth(200);
  lineEdit->setEchoMode(QLineEdit::Password);
  lineEdit->setPlaceholderText(tr("Enter Password"));
  connect(lineEdit, &QLineEdit::returnPressed, dialog, &QDialog::accept);
  QSpacerItem *spacer0 = new QSpacerItem(1, 5);
  QSpacerItem *spacer = new QSpacerItem(1, 5);
  QPushButton *cancelButton = new QPushButton (symbolicIcon::icon(":icons/dialog-error.svg"), tr("Cancel"));
  QPushButton *okButton = new QPushButton (symbolicIcon::icon(":icons/dialog-ok.svg"), tr("OK"));
  okButton->setDefault(true);
  connect(cancelButton, &QAbstractButton::clicked, dialog, &QDialog::reject);
  connect(okButton, &QAbstractButton::clicked, dialog, &QDialog::accept);
  QCheckBox *box = new QCheckBox(tr("Encrypt the file list"));
  QLabel *label = new QLabel("<center><i>" + tr("This will take effect after files/folders are added.") + "</i></center>" );
  label->setWordWrap(true);

  grid->addWidget(lineEdit, 0, 0, 1, 3);
  grid->addWidget(box, 1, 0, 1, 3);
  grid->addItem(spacer0, 2, 0);
  grid->addWidget(label, 3, 0, 1, 3);
  grid->addItem(spacer, 4, 0);
  grid->addWidget(cancelButton, 5, 0, 1, 2, Qt::AlignRight);
  grid->addWidget(okButton, 5, 2, Qt::AlignCenter);
  grid->setColumnStretch(1, 1);
  grid->setRowStretch(4, 1);

  if(!listEncryptionBox) {
    box->setVisible(false);
    label->setVisible(false);
  }
  else if (forceListEncryption) {
    box->setChecked(true);
    box->setEnabled(false);
  }

  dialog->setLayout(grid);
  dialog->setMinimumSize(dialog->sizeHint());

  bool cursorWasBusy(QGuiApplication::overrideCursor() != nullptr);
  if (cursorWasBusy)
    QGuiApplication::restoreOverrideCursor();

  bool res = true;
  switch (dialog->exec()) {
  case QDialog::Accepted:
    if (lineEdit->text().isEmpty())
      res = false;
    else {
      BACKEND->setPswrd(lineEdit->text());
      if (box->isChecked())
        BACKEND->encryptFileList();
      if (listEncryptionBox)
        lastPswrd_ = lineEdit->text();
    }
    delete dialog;
    break;
  case QDialog::Rejected:
  default:
    delete dialog;
    res = false;
    break;
  }

  if (!res) { // don't proceed with autoextraction
    disconnect(BACKEND, &Backend::loadingFinished, this, &mainWin::autoextractFiles);
    disconnect(BACKEND, &Backend::extractionFinished, this, &mainWin::nextAutoExtraction);
    axFileList_.clear();
    enableActions(true);
    statusProgress_->setRange(0, 0);
    statusProgress_->setValue(0);
    statusProgress_->setVisible(false);
  }
  else if (cursorWasBusy && QGuiApplication::overrideCursor() == nullptr)
    QGuiApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

  return res;
}

void mainWin::extractFiles() {
  if (BACKEND->isEncrypted() && BACKEND->getPswrd().isEmpty() && !pswrdDialog())
    return;
  bool cursorWasBusy(QGuiApplication::overrideCursor() != nullptr);
  if (cursorWasBusy) // simple extraction: after loading but before updating tree
    QGuiApplication::restoreOverrideCursor();
  QString dir = QFileDialog::getExistingDirectory(this, tr("Extract Into Directory"), lastPath_);
  if (cursorWasBusy && QGuiApplication::overrideCursor() == nullptr)
    QGuiApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
  if (dir.isEmpty()) return;
  lastPath_ = dir;
  updateTree_ = false;
  reapplyFilter_ = false;
  textLabel_->setText(tr("Extracting..."));
  BACKEND->startExtract(dir);
}

void mainWin::autoextractFiles() {
  QString dir = BACKEND->currentFile().section("/",0,-2);
  if (dir.isEmpty()) return;
  if (BACKEND->isEncrypted() && BACKEND->getPswrd().isEmpty() && !pswrdDialog())
    return;
  updateTree_ = false;
  reapplyFilter_ = false;
  textLabel_->setText(tr("Extracting..."));
  BACKEND->startExtract(dir);
}

void mainWin::nextAutoExtraction() {
  if (axFileList_.isEmpty()) {
    processIsRunning_ = false;
    QTimer::singleShot(500, this, &QWidget::close);
  }
  else {
    BACKEND->loadFile(axFileList_.first());
    axFileList_.removeFirst();
  }
}

void mainWin::simpleExtractFiles() {
  disconnect(BACKEND, &Backend::loadingSuccessful, this, &mainWin::simpleExtractFiles);
  extractFiles();
}

void mainWin::autoArchiveFiles() { // no protection against overwriting
  disconnect(BACKEND, &Backend::loadingSuccessful, this, &mainWin::autoArchiveFiles);
  textLabel_->setText(tr("Adding Items..."));
  BACKEND->startAdd(aaFileList_);
}

void mainWin::simpleArchivetFiles() {
  disconnect(BACKEND, &Backend::loadingSuccessful, this, &mainWin::simpleArchivetFiles);
  textLabel_->setText(tr("Adding Items..."));
  BACKEND->startAdd(saFileList_);
}

void mainWin::extractSelection() {
  if (ui->tree_contents->currentItem() == nullptr) return; // nothing selected

  if (BACKEND->isEncrypted() && BACKEND->getPswrd().isEmpty()) {
    /* not needed because there's no selective extraction for 7z */
    if (!pswrdDialog()) return;
  }

  QList<QTreeWidgetItem*> sel = ui->tree_contents->selectedItems();
  if (sel.isEmpty()) {
    sel << ui->tree_contents->currentItem();
  }
  QStringList selList;
  const QString singleRoot = BACKEND->singleRoot();
  for (int i = 0; i < sel.length(); i++) {
    if (sel[i]->whatsThis(0) == singleRoot) { // total extraction
      selList.clear();
      break;
    }
    selList << sel[i]->whatsThis(0);
  }

  QString dir = QFileDialog::getExistingDirectory(this, tr("Extract Into Directory"), lastPath_);
  if (dir.isEmpty()) return;

  /* check overwriting with partial extractions */
  bool overwrite(false);
  if (!selList.isEmpty()) {
    selList.sort();
    for (const auto &file : qAsConst(selList)) {
        if (QFile::exists(dir + "/" + file.section("/",-1))) {
          QMessageBox::StandardButton btn = QMessageBox::question(this,
                                                                  tr("Question"),
                                                                  tr("Some files will be overwritten.\nDo you want to continue?\n"));
          if (btn == QMessageBox::No)
            return;
          overwrite = true;
          break;
        }
    }
  }

  lastPath_ = dir;
  updateTree_ = false;
  reapplyFilter_ = false;
  textLabel_->setText(tr("Extracting..."));
  BACKEND->startExtract(dir, selList, overwrite);
}

void mainWin::viewFile(QTreeWidgetItem *it) {
  if (it->text(1).isEmpty()) return; // it's a directory item
  if (BACKEND->isEncrypted() && BACKEND->getPswrd().isEmpty()
      && BACKEND->isEncryptedPath(it->whatsThis(0))
      && !BACKEND->isSingleExtracted(it->whatsThis(0))
      && !pswrdDialog()) {
    return;
  }
  updateTree_ = false;
  reapplyFilter_ = false;
  textLabel_->setText(tr("Extracting..."));
  BACKEND->startViewFile(it->whatsThis(0));
}

void mainWin::expandOrView(QTreeWidgetItem *it) {
  if (it->text(1).isEmpty()) { // it's a directory item
    if (ui->tree_contents->isExpanded(ui->tree_contents->getIndexFromItem(it)))
      ui->tree_contents->collapseItem(it);
    else
      ui->tree_contents->expandItem(it);
  }
  else viewFile(it);
}

static inline QString displaySize(const qint64 size) {
  static const QStringList labels = {"B", "K", "M", "G", "T"};
  int i = 0;
  qreal displaySize = size;
  while (displaySize > 1024) {
    displaySize /= 1024.0;
    ++i;
  }
  return (QString::number(qRound(displaySize)) + labels[i]);
}

QPixmap mainWin::emblemize(const QString iconName, const QSize& icnSize, bool lock) {
  QString emblemName = lock ? ".lock" : ".link";
  int w = icnSize.width();
  int emblemSize = w <= 32 ? 16 : 24;
  QPixmap pix;
  if (!QPixmapCache::find(iconName + emblemName, &pix)) {
    int pixelRatio = qApp->devicePixelRatio();
    QPixmap icn = QIcon::fromTheme(iconName).pixmap(icnSize * pixelRatio);
    int offset = 0;
    QPixmap emblem;
    if (lock) {
      emblem = QIcon(":icons/emblem-lock.svg").pixmap(emblemSize*pixelRatio, emblemSize*pixelRatio);
      offset = (w - emblemSize) * pixelRatio;
    }
    else
      emblem = QIcon(":icons/emblem-symbolic-link.svg").pixmap(emblemSize*pixelRatio, emblemSize*pixelRatio);
    pix = QPixmap(icnSize * pixelRatio);
    pix.fill(Qt::transparent);
    QPainter painter(&pix);
    painter.drawPixmap(0, 0, icn);
    painter.drawPixmap(offset, offset, emblem);
    QPixmapCache::insert(iconName + emblemName, pix);
  }
  return pix;
}

void mainWin::updateTree() {
  setUpdatesEnabled(false);
  QStringList files = BACKEND->hierarchy();
  files.sort();
  bool itemAdded = false;

  /* NOTE: With big archives, QHash is much faster than finding items. */
  /* remove items that aren't in the archive and get the remaining items */
  QHash<QString, QTreeWidgetItem*> allPrevItems = cleanTree(files.size() > 10000 ? QStringList() : files);
  QHash<const QString&, QTreeWidgetItem*> dirs; // keep track of directory items

  for (const auto& thisFile : qAsConst(files)) {
    QTreeWidgetItem *item = allPrevItems.value(thisFile);
    if (item != nullptr) { // already in the tree widget
      if(BACKEND->isDir(thisFile))
        dirs.insert(thisFile, item);
      continue;
    }

    QString mime;
    if (!BACKEND->isDir(thisFile))
      mime = BACKEND->getMimeType(thisFile.section("/", -1));
    QTreeWidgetItem *it = new QTreeWidgetItem();

    /* set texts and icons */
    QSize icnSize = ui->tree_contents->iconSize();
    it->setText(0, thisFile.section("/", -1));
    if (!mime.isEmpty()) {
      it->setText(3, "0"); // to put it after directory items
      if (!BACKEND->isLink(thisFile)) {
        it->setText(1, mime);
        it->setText(2, displaySize(BACKEND->size(thisFile)));

        if (icnSize.width() > 16 && BACKEND->isEncryptedPath(thisFile)) {
          it->setData(3, Qt::UserRole, "lock"); // to be used in cleanTree()
          it->setIcon(0, QIcon(emblemize(mime.replace('/', '-'), icnSize, true)));
        }
        else
          it->setIcon(0, QIcon::fromTheme(mime.replace('/', '-')));
      }
      else {
        it->setText(1, tr("Link To: %1").arg(BACKEND->linkTo(thisFile)));

        const QString targetMime = BACKEND->getMimeType(BACKEND->linkTo(thisFile)
                                                        .section("/",-1))
                                  .replace('/', '-');
        if (!targetMime.isEmpty()) {
          if (icnSize.width() > 16)
            it->setIcon(0, QIcon(emblemize(targetMime, icnSize, false)));
          else
            it->setIcon(0, QIcon::fromTheme(targetMime));
        }
        else
          it->setIcon(0, QIcon(":icons/emblem-symbolic-link.svg"));
      }
    }
    else
      it->setIcon(0, QIcon::fromTheme("folder"));

    it->setWhatsThis(0, thisFile);
    it->setData(2, Qt::UserRole, BACKEND->sizeString(thisFile)); // to track the file size quickly

    /* add items to the tree appropriately */
    if (thisFile.contains("/")) {
      QTreeWidgetItem *parent = dirs.value(thisFile.section("/", 0, -2));
      if (parent)
        parent->addChild(it);
      else {
        /* check the parent paths, create their items if not existing,
           and add children to their parents */
        QStringList sections = thisFile.split("/", QString::SkipEmptyParts);
        if (!sections.isEmpty()) { // can be empty in rare cases (with "application/x-archive", for example)
          sections.removeLast();
          QTreeWidgetItem *parentItem = nullptr;
          QString theFile;
          for (const auto& thisSection : qAsConst(sections)) {
            theFile += (theFile.isEmpty() ? QString() : "/") + thisSection;
            QTreeWidgetItem *thisParent = dirs.value(theFile);
            if (!thisParent) {
              QTreeWidgetItem *thisItem = new QTreeWidgetItem();
              thisItem->setText(0, thisSection);
              thisItem->setIcon(0, QIcon::fromTheme("folder"));
              thisItem->setWhatsThis(0, theFile);
              thisItem->setData(2, Qt::UserRole, BACKEND->sizeString(theFile));

              dirs.insert(theFile, thisItem);
              if (parentItem)
                parentItem->addChild(thisItem);
              else
                ui->tree_contents->addTopLevelItem(thisItem);
              parentItem = thisItem;
            }
            else
              parentItem = thisParent; // already handled
          }
          parentItem->addChild(it);
        }
      }
    }
    else
      ui->tree_contents->addTopLevelItem(it);

    if(BACKEND->isDir(thisFile))
      dirs.insert(thisFile, it);

    itemAdded = true;
  }

  if (itemAdded) {
    /* adjust column sizes */
    QTimer::singleShot(0, this, [this]() {
      ui->tree_contents->resizeColumnToContents(2);
    });
    QTimer::singleShot(0, this, [this]() {
      ui->tree_contents->resizeColumnToContents(1);
    });
    /* sort items by their names but put directory items first */
    QTimer::singleShot(0, this, [this]() {
      ui->tree_contents->sortItems(0, Qt::AscendingOrder);
      ui->tree_contents->sortItems(3, Qt::AscendingOrder);
    });
  }

  setUpdatesEnabled(true);
  ui->tree_contents->setEnabled(true);
  enableActions(true);
  if (expandAll_) {
    if (itemAdded) {
      ui->tree_contents->expandAll();
      QTimer::singleShot(0, this, [this]() {
        if(QAbstractItemModel *model = ui->tree_contents->model()) {
          QModelIndex firstIndx = model->index(0, 0);
          if(firstIndx.isValid())
            ui->tree_contents->selectionModel()->setCurrentIndex(firstIndx, QItemSelectionModel::NoUpdate);
        }
      });
    }
    expandAll_ = false;
  }
  ui->tree_contents->setFocus();
}

void mainWin::enableActions(bool enable) {
  ui->actionOpen->setEnabled(enable);
  ui->actionNew->setEnabled(enable);
  ui->actionQuit->setEnabled(enable);
  ui->actionAddFile->setEnabled(enable);
  ui->actionRemoveFile->setEnabled(enable);
  ui->actionExtractAll->setEnabled(enable);
  ui->actionAddDir->setEnabled(enable);
  ui->actionExtractSel->setEnabled(enable);
  ui->actionPassword->setEnabled(enable);
  ui->actionExpand->setEnabled(enable);
  ui->actionCollapse->setEnabled(enable);
  ui->actionAbout->setEnabled(enable);
  ui->lineEdit->setEnabled(enable);

  if (enable && reapplyFilter_ && !ui->lineEdit->text().isEmpty())
    reallyApplyFilter();
  reapplyFilter_ = true;
}

void mainWin::procStarting() {
  processIsRunning_ = true;
  enableActions(false);

  statusProgress_->setRange(0, 0);
  statusProgress_->setValue(0);
  statusProgress_->setVisible(true);
  iconLabel_->setVisible(false);
  ui->tree_contents->setEnabled(false);

  ui->label->setVisible(true);
  ui->label_archive->setVisible(true);
  ui->label_archive->setText(BACKEND->currentFile());

  if (QGuiApplication::overrideCursor() == nullptr)
    QGuiApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
}

void mainWin::procFinished(bool success, const QString& msg) {
  processIsRunning_ = false;

  ui->label->setVisible(true);
  ui->label_archive->setVisible(true);
  ui->label_archive->setText(BACKEND->currentFile());

  if (updateTree_)
    updateTree();
  else {
    ui->tree_contents->setEnabled(true);
    enableActions(true);
  }
  statusProgress_->setRange(0, 0);
  statusProgress_->setValue(0);
  statusProgress_->setVisible(false);
  lastMsg_ = msg;

  QTreeWidgetItem *cur = ui->tree_contents->currentItem();
  if (cur && ui->tree_contents->selectedItems().contains(cur))
    textLabel_->setText(cur->whatsThis(0));
  else
    textLabel_->setText(lastMsg_);

  iconLabel_->setVisible(true);
  if (success) {
    if (BACKEND->isEncrypted()) {
      if (BACKEND->getPswrd().isEmpty())
        iconLabel_->setPixmap(symbolicIcon::icon(":icons/locked.svg").pixmap(16, 16));
      else
        iconLabel_->setPixmap(symbolicIcon::icon(":icons/unlocked.svg").pixmap(16, 16));
    }
    else
      iconLabel_->setPixmap(symbolicIcon::icon(":icons/dialog-ok.svg").pixmap(16, 16));
  }
  else
    iconLabel_->setPixmap(symbolicIcon::icon(":icons/dialog-error.svg").pixmap(16, 16));
  QFileInfo info(BACKEND->currentFile());
  bool canmodify = info.isWritable();
  if (!info.exists())
    canmodify = QFileInfo(BACKEND->currentFile().section("/", 0, -2)).isWritable();
  canmodify = canmodify && BACKEND->canModify(); // also include the file type limitations
  ui->actionAddFile->setEnabled(canmodify && (!BACKEND->isGzip() || ui->tree_contents->topLevelItemCount() == 0));
  ui->actionRemoveFile->setEnabled(canmodify && info.exists() && !BACKEND->isGzip());
  ui->actionExtractAll->setEnabled(info.exists() && ui->tree_contents->topLevelItemCount() > 0);
  ui->actionExtractSel->setEnabled(info.exists() && !ui->tree_contents->selectedItems().isEmpty());
  ui->actionAddDir->setEnabled(canmodify && !BACKEND->isGzip());
  ui->actionPassword->setEnabled(BACKEND->is7z());

  updateTree_ = true;
  if (close_)
    QTimer::singleShot(500, this, &QWidget::close);

  if (QGuiApplication::overrideCursor() != nullptr)
    QGuiApplication::restoreOverrideCursor();
}

void mainWin::procUpdate(int percent, const QString& txt) {
  statusProgress_->setMaximum(percent < 0 ? 0 : 100);
  statusProgress_->setValue(percent);
  if (!txt.isEmpty())
    textLabel_->setText(txt);
}

void mainWin::openEncryptedList(const QString& path) {
  if (!pswrdDialog()) {
    processIsRunning_ = false; // it's safe to exit
    return;
  }
  axFileList_.removeOne(path);
  BACKEND->loadFile(path, true);
}

void mainWin::selectionChanged() {
  ui->actionExtractSel->setEnabled(!ui->tree_contents->selectedItems().isEmpty());
  QTreeWidgetItem *cur = ui->tree_contents->currentItem();
  if (cur && ui->tree_contents->selectedItems().contains(cur))
    textLabel_->setText(cur->whatsThis(0));
  else
    textLabel_->setText(lastMsg_);
}

void mainWin::prefDialog() {
    PrefDialog dlg(this);
    dlg.exec();
}

void mainWin::aboutDialog() {
  class AboutDialog : public QDialog {
  public:
    explicit AboutDialog(QWidget* parent = nullptr, Qt::WindowFlags f = 0) : QDialog(parent, f) {
      aboutUi.setupUi(this);
      aboutUi.textLabel->setOpenExternalLinks(true);
    }
    void setTabTexts(const QString& first, const QString& sec) {
      aboutUi.tabWidget->setTabText(0, first);
      aboutUi.tabWidget->setTabText(1, sec);
    }
    void setMainIcon(const QIcon& icn) {
      aboutUi.iconLabel->setPixmap(icn.pixmap (64, 64));
    }
    void settMainTitle(const QString& title) {
      aboutUi.titleLabel->setText(title);
    }
    void setMainText(const QString& txt) {
      aboutUi.textLabel->setText(txt);
    }
  private:
    Ui::AboutDialog aboutUi;
  };

  AboutDialog dialog (this);
  dialog.setMainIcon(QIcon::fromTheme("utilities-file-archiver"));
  dialog.settMainTitle (QString("<center><b><big>%1 %2</big></b></center><br>").arg(qApp->applicationName()).arg(qApp->applicationVersion()));
  dialog.setMainText("<center> " + tr("A simple Qt archive manager") + " </center>\n<center> "
                     + tr("based on libarchive, gzip and 7z") + " </center><br><center> "
                     + tr("Author")+": <a href='mailto:tsujan2000@gmail.com?Subject=My%20Subject'>Pedram Pourang ("
                     + tr("aka.") + " Tsu Jan)</a> </center><p></p>");
  dialog.setTabTexts(tr("About Arqiver"), tr("Translators"));
  dialog.setWindowTitle(tr("About Arqiver"));
  dialog.setWindowModality(Qt::WindowModal);
  dialog.exec();
}

}
