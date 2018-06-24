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

#include "mainWin.h"
#include "ui_mainWin.h"
#include "svgicons.h"

#include <QMessageBox>
#include <QFileDialog>
#include <QMimeDatabase>
#include <QMimeData>
#include <QTimer>

#include <unistd.h> // getuid

namespace Arqiver {

mainWin::mainWin() : QMainWindow(), ui(new Ui::mainWin) {
  ui->setupUi(this);

  lastPath_ = QDir::homePath();
  expandAll_ = false;
  close_ = false;

  if (getuid() == 0)
    setWindowTitle("Arqiver (" + tr("Root") + ")");

  /* icons */
  setWindowIcon(QIcon::fromTheme("utilities-file-archiver"));
  ui->action_New->setIcon(symbolicIcon::icon(":icons/document-new.svg"));
  ui->action_Open->setIcon(symbolicIcon::icon(":icons/document-open.svg"));
  ui->action_Quit->setIcon(symbolicIcon::icon(":icons/application-exit.svg"));
  ui->actionAdd_File->setIcon(symbolicIcon::icon(":icons/archive-insert.svg"));
  ui->actionAdd_Dirs->setIcon(symbolicIcon::icon(":icons/archive-insert-directory.svg"));
  ui->actionRemove_File->setIcon(symbolicIcon::icon(":icons/archive-remove.svg"));
  ui->actionExtract_All->setIcon(symbolicIcon::icon(":icons/archive-extract.svg"));
  ui->actionExtract_Sel->setIcon(symbolicIcon::icon(":icons/edit-select-all.svg"));

  ui->label->setVisible(false);
  ui->label_archive->setVisible(false);
  ui->progressBar->setVisible(false);
  ui->label_progress->setVisible(false);
  ui->label_progress_icon->setVisible(false);

  ui->actionAdd_File->setEnabled(false);
  ui->actionRemove_File->setEnabled(false);
  ui->actionExtract_All->setEnabled(false);
  ui->actionAdd_Dirs->setEnabled(false);
  ui->actionExtract_Sel->setEnabled(false);

  BACKEND = new Backend(this);
  connect(BACKEND, &Backend::ProcessStarting, this, &mainWin::ProcStarting);
  connect(BACKEND, &Backend::ProcessFinished, this, &mainWin::ProcFinished);
  connect(BACKEND, &Backend::ProgressUpdate, this, &mainWin::ProcUpdate);

  QWidget *spacer = new QWidget(this);
  spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  ui->toolBar->insertWidget(ui->actionAdd_File, spacer);

  connect(ui->action_New, &QAction::triggered, this, &mainWin::NewArchive);
  connect(ui->action_Open, &QAction::triggered, this, &mainWin::OpenArchive);
  connect(ui->action_Quit, &QAction::triggered, this, &mainWin::close);
  connect(ui->actionAdd_File, &QAction::triggered, this, &mainWin::addFiles);
  connect(ui->actionRemove_File, &QAction::triggered, this, &mainWin::remFiles);
  connect(ui->actionExtract_All, &QAction::triggered, this, &mainWin::extractFiles);
  connect(ui->actionExtract_Sel, &QAction::triggered, this, &mainWin::extractSelection);
  connect(ui->actionAdd_Dirs, &QAction::triggered, this, &mainWin::addDirs);
  connect(ui->tree_contents, &QTreeWidget::itemDoubleClicked, this, &mainWin::ViewFile);
  connect(ui->tree_contents, &QTreeWidget::itemSelectionChanged, this, &mainWin::selectionChanged);
  connect(ui->tree_contents, &TreeWidget::dragStarted, this, &mainWin::extractFile);

  /* the labels and column sizes of the header */
  ui->tree_contents->setHeaderLabels(QStringList() << tr("File") << tr("MimeType") << tr("Size"));
  ui->tree_contents->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  QTimer::singleShot (0, this, [this]() {
    ui->tree_contents->resizeColumnToContents(2);
  });
  QTimer::singleShot (0, this, [this]() {
    ui->tree_contents->resizeColumnToContents(1);
  });

  /* support file dropping into the window */
  setAcceptDrops(true);
}

mainWin::~mainWin() {
}

void mainWin::LoadArguments(QStringList args) {
  int action = -1; // 0: autoExtract, 1: autoArchive, 2: simple extract
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
    }
    else {
      files << args[i];
    }
  }

  if (files.isEmpty()) return;
  if (!files.at(0).isEmpty())
    lastPath_ = files.at(0).section("/", 0, -2);

  ui->label_progress->setText(tr("Opening Archive..."));
  if (action == 0) {
    connect(BACKEND, &Backend::FileLoaded, this, &mainWin::autoextractFiles);
    connect(BACKEND, &Backend::ExtractSuccessful, [this] {close_ = true;});
  }
  else if (action == 1) {
    aaFileList_ = files;
    aaFileList_.removeFirst();
    connect(BACKEND, &Backend::FileLoaded, this, &mainWin::autoArchiveFiles);
    connect(BACKEND, &Backend::ArchivalSuccessful, [this] {close_ = true;});
  }
  else if (action == 2 && files.length() == 2) {
    sxFile_ = files[0];
    sxPath_ = files[1];
    connect(BACKEND, &Backend::FileLoaded, this, &mainWin::simpleExtractFiles);
    connect(BACKEND, &Backend::ExtractSuccessful, [this] {close_ = true;});
  }
  else
    expandAll_ = true;
  BACKEND->loadFile(files[0]);
}

QTreeWidgetItem* mainWin::findItem(QString path, QTreeWidgetItem *start) {
  if (start == 0) {
    for (int i = 0; i < ui->tree_contents->topLevelItemCount(); i++) {
      if (ui->tree_contents->topLevelItem(i)->whatsThis(0) == path)
        return ui->tree_contents->topLevelItem(i);
      else if (path.startsWith(ui->tree_contents->topLevelItem(i)->whatsThis(0)+"/"))
        return findItem(path, ui->tree_contents->topLevelItem(i));
    }
  }
  else {
    for (int i = 0; i < start->childCount(); i++) {
      if (start->child(i)->whatsThis(0) == path)
        return start->child(i);
      else if (path.startsWith(start->child(i)->whatsThis(0)+"/"))
        return findItem(path, start->child(i));
    }
  }
  return nullptr;
}

bool mainWin::cleanTree(QStringList list) {
  if (list.isEmpty() && ui->tree_contents->topLevelItemCount() > 0) {
    ui->tree_contents->clear();
    return true;
  }
  bool changed = false;
  QTreeWidgetItemIterator it(ui->tree_contents);
  while (*it) {
    if (!list.contains((*it)->whatsThis(0))) {
      qDeleteAll((*it)->takeChildren());
      delete *it;
      changed = true;
    }
    else ++it;
  }
  return changed;
}

QString mainWin::CreateFileTypes() {
  static QString fileTypes;
  if (fileTypes.isEmpty()) {
    QStringList types;
    types << QString(tr("All Types %1")).arg("(*.tar.gz *.tar.xz *.tar.bz *.tar.bz2 *.tar.lzma *.tar *.zip *.tgz *.txz *.tbz *.tbz2 *.tlz *.cpio *.pax *.ar *.shar *.7z)");
    types << tr("Uncompressed Archive (*.tar)");
    types << tr("GZip Compressed Archive (*.tar.gz *.tgz)");
    types << tr("BZip Compressed Archive (*.tar.bz *.tbz)");
    types << tr("BZip2 Compressed Archive (*.tar.bz2 *.tbz2)");
    types << tr("LMZA Compressed Archive (*.tar.lzma *.tlz)");
    types << tr("XZ Compressed Archive (*.tar.xz *.txz)");
    types << tr("CPIO Archive (*.cpio)");
    types << tr("PAX Archive (*.pax)");
    types << tr("AR Archive (*.ar)");
    types << tr("SHAR Archive (*.shar)");
    types << tr("Zip Archive (*.zip)");
    types << tr("7-Zip Archive (*.7z)");
    fileTypes = types.join(";;");
  }
  return fileTypes;
}

QString mainWin::OpenFileTypes(){
  static QString fileTypes;
  if (fileTypes.isEmpty()) {
    QStringList types;
    types << QString(tr("All Known Types %1")).arg("(*.tar.gz *.tar.xz *.tar.bz *.tar.bz2 *.tar.lzma *.tar *.zip *.tgz *.txz *.tbz *.tbz2 *.tlz *.cpio *.pax *.ar *.shar *.7z *.iso *.img *.xar *.jar *.rpm)");
    types << tr("Uncompressed Archive (*.tar)");
    types << tr("GZip Compressed Archive (*.tar.gz *.tgz)");
    types << tr("BZip Compressed Archive (*.tar.bz *.tbz)");
    types << tr("BZip2 Compressed Archive (*.tar.bz2 *.tbz2)");
    types << tr("XZ Compressed Archive (*.tar.xz *.txz)");
    types << tr("LMZA Compressed Archive (*.tar.lzma *.tlz)");
    types << tr("CPIO Archive (*.cpio)");
    types << tr("PAX Archive (*.pax)");
    types << tr("AR Archive (*.ar)");
    types << tr("SHAR Archive (*.shar)");
    types << tr("Zip Archive (*.zip)");
    types << tr("7-Zip Archive (*.7z)");
    types << tr("READ-ONLY: ISO image (*.iso *.img)");
    types << tr("READ-ONLY: XAR archive (*.xar)");
    types << tr("READ-ONLY: Java archive (*.jar)");
    types << tr("READ-ONLY: RedHat Package (*.rpm)");
    types << tr("Show All Files (*)");
    fileTypes = types.join(";;");
  }
  return fileTypes;
}

void mainWin::NewArchive() {
  QString file;

  bool retry(true);
  QString path = lastPath_;
  QString nameFilter;
  while (retry) {
    QFileDialog dlg(this, tr("Create Archive"), path, CreateFileTypes());
    dlg.setAcceptMode(QFileDialog::AcceptSave);
    dlg.setFileMode(QFileDialog::AnyFile);
    dlg.selectNameFilter(nameFilter);
    if (dlg.exec()) {
      nameFilter = dlg.selectedNameFilter();
      file = dlg.selectedFiles().at(0);
    }
    else return;
    if (file.isEmpty()) return;
    if (file.indexOf('.') == -1) {
      QString filter = nameFilter;
      auto left = filter.indexOf('(');
      if (left != -1) {
        ++left;
        auto right = filter.indexOf(')', left);
        if (right == -1)
          right = filter.length();
        filter = filter.mid(left, right - left);
      }
      auto list = filter.simplified().split(' ');
      if (!list.isEmpty()) {
        filter = list.at(0);
        if (!filter.isEmpty()) {
          filter.remove("*");
          if (!filter.isEmpty())
            file += filter;
        }
      }
      if (file.indexOf('.') > -1 && QFile::exists(file)) {
          QMessageBox::StandardButton btn = QMessageBox::question(this,
                                                                  tr("Question"),
                                                                  tr("The following archive already exists:")
                                                                  + QString("\n\n%1\n\n").arg(file.section('/', -1))
                                                                  + tr("Do you want to replace it?\n"));
          if (btn == QMessageBox::No)
            path = file;
          else retry = false;
      }
      else retry = false;
    }
    else retry = false; // the input had an extension
  }

  lastPath_ = file.section("/", 0, -2);

  ui->label_progress->setText(""); //just clear it (this action is instant)
  BACKEND->loadFile(file);
}

void mainWin::OpenArchive() {
  QString file = QFileDialog::getOpenFileName(this, tr("Open Archive"), lastPath_, OpenFileTypes());
  if (file.isEmpty()) return;
  lastPath_ = file.section("/", 0, -2);
  ui->label_progress->setText(tr("Opening Archive..."));
  expandAll_ = true;
  BACKEND->loadFile(file);
}

static inline QString getMimeType(const QString &fname) {
  QMimeDatabase mimeDatabase;
  QMimeType mimeType = mimeDatabase.mimeTypeForFile(QFileInfo(fname));
  return mimeType.name();
}

void mainWin::dragEnterEvent(QDragEnterEvent *event) {
  if (event->mimeData()->hasUrls()) {
    const QList<QUrl> urlList = event->mimeData()->urls();
    if (!urlList.isEmpty()) {
      QString file = urlList.at(0).toLocalFile();
      if (!file.isEmpty()) {
        static const QStringList supportedArcives = {"application/x-compressed-tar",
                                                     "application/x-bzip",
                                                     "application/x-bzip-compressed-tar",
                                                     "application/x-xz-compressed-tar",
                                                     "application/x-lzma-compressed-tar",
                                                     "application/x-cpio",
                                                     "application/x-tar",
                                                     "application/x-archive",
                                                     "application/x-shar",
                                                     "application/zip",
                                                     "application/x-7z-compressed",
                                                     "application/x-cd-image",
                                                     "application/x-raw-disk-image",
                                                     "application/x-xar",
                                                     "application/x-java-archive",
                                                     "application/x-source-rpm",
                                                     "application/x-rpm"};
        QString mime = getMimeType(file.section("/",-1));
        if (!supportedArcives.contains(mime)) {
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
  if (event->mimeData()->hasFormat ("application/limina-item"))
    event->ignore(); // don't drop from inside the view (not needed)
  else {
    const QList<QUrl> urlList = event->mimeData()->urls();
    if (!urlList.isEmpty()) {
      QString file = urlList.at(0).toLocalFile();
      if (!file.isEmpty()) {
        lastPath_ = file.section("/", 0, -2);
        ui->label_progress->setText(tr("Opening Archive..."));
        expandAll_ = true;
        BACKEND->loadFile(file);
      }
    }
    event->acceptProposedAction();
  }
}

void mainWin::addFiles() {
  QStringList files = QFileDialog::getOpenFileNames(this, tr("Add to Archive"), lastPath_);
  if (files.isEmpty()) return;
  if (!files.at(0).isEmpty())
    lastPath_ = files.at(0).section("/", 0, -2);
  ui->label_progress->setText(tr("Adding Items..."));
  BACKEND->startAdd(files);
}

void mainWin::addDirs() {
  QString dirs = QFileDialog::getExistingDirectory(this, tr("Add to Archive"), lastPath_ );
  if (dirs.isEmpty()) return;
  lastPath_ = dirs;
  ui->label_progress->setText(tr("Adding Items..."));
  BACKEND->startAdd(QStringList() << dirs);
}

void mainWin::remFiles() {
  QList<QTreeWidgetItem*> sel = ui->tree_contents->selectedItems();
  QStringList items;
  for (int i = 0; i < sel.length(); i++){
    items << sel[i]->whatsThis(0);
  }
  items.removeDuplicates();
  ui->label_progress->setText(tr("Removing Items..."));
  BACKEND->startRemove(items);
}

void mainWin::extractFile(QTreeWidgetItem *it) {
  if (it->text(1).isEmpty()) return; // it's a directory item
  ui->label_progress->setText(tr("Extracting..."));
  it->setData(0, Qt::UserRole, BACKEND->extractFile(it->whatsThis(0)));
}

void mainWin::extractFiles() {
  QString dir = QFileDialog::getExistingDirectory(this, tr("Extract Into Directory"), lastPath_);
  if (dir.isEmpty()) return;
  lastPath_ = dir;
  ui->label_progress->setText(tr("Extracting..."));
  BACKEND->startExtract(dir, true);
}

void mainWin::autoextractFiles() {
  disconnect(BACKEND, &Backend::FileLoaded, this, &mainWin::autoextractFiles);
  QString dir = BACKEND->currentFile().section("/",0,-2);
  if (dir.isEmpty()) return;
  ui->label_progress->setText(tr("Extracting..."));
  BACKEND->startExtract(dir, true);
}

void mainWin::simpleExtractFiles() {
  disconnect(BACKEND, &Backend::FileLoaded, this, &mainWin::simpleExtractFiles);
  QString dir = sxPath_;
  ui->label_progress->setText(tr("Extracting..."));
  BACKEND->startExtract(dir, true);
}

void mainWin::autoArchiveFiles() { // no protection against overwriting
  disconnect(BACKEND, &Backend::FileLoaded, this, &mainWin::autoArchiveFiles);
  ui->label_progress->setText(tr("Adding Items..."));
  BACKEND->startAdd(aaFileList_, true);
}

void mainWin::extractSelection(){
  if (ui->tree_contents->currentItem() == 0) return; // nothing selected
  QList<QTreeWidgetItem*> sel = ui->tree_contents->selectedItems();
  if (sel.isEmpty()) {
    sel << ui->tree_contents->currentItem();
  }
  QStringList selList;
  for (int i = 0; i < sel.length(); i++) {
    selList << sel[i]->whatsThis(0);
  }
  selList.removeDuplicates();
  QString dir = QFileDialog::getExistingDirectory(this, tr("Extract Into Directory"), lastPath_);
  if (dir.isEmpty()) return;
  lastPath_ = dir;
  ui->label_progress->setText(tr("Extracting..."));
  BACKEND->startExtract(dir, true, selList);
}

void mainWin::ViewFile(QTreeWidgetItem *it) {
  if (it->text(1).isEmpty()) return; // it's a directory item
  ui->label_progress->setText(tr("Extracting..."));
  BACKEND->startViewFile(it->whatsThis(0));
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

void mainWin::UpdateTree() {
  setEnabled(false);
  QStringList files = BACKEND->heirarchy();
  files.sort();
  //Remove any entries for file no longer in the archive
  bool changed = cleanTree(files);
  for (int i = 0; i < files.length(); i++) {
    if (findItem(files[i]) != nullptr)
      continue; // already in the tree widget
    QString mime;
    if (!BACKEND->isDir(files[i]))
      mime = getMimeType(files[i].section("/",-1));
    QTreeWidgetItem *it = new QTreeWidgetItem();
    it->setText(0, files[i].section("/",-1) );
    if (!mime.isEmpty()) {
      if (!BACKEND->isLink(files[i])) {
        it->setText(1, mime);
        it->setText(2, displaySize( BACKEND->size(files[i])) );
      }
      else
        it->setText(1, QString(tr("Link To: %1")).arg(BACKEND->linkTo(files[i]) ) );
    }
    it->setWhatsThis(0, files[i]);
    if (mime.isEmpty()) {
      it->setIcon(0, QIcon::fromTheme("folder"));
      it->setText(1,""); //clear the mimetype
    }
    else if (BACKEND->isLink(files[i]))
      it->setIcon(0, QIcon::fromTheme("emblem-symbolic-link") );
    else
      it->setIcon(0, QIcon::fromTheme(mime.replace('/', '-')));
    //Now find which item to add this too
    if (files[i].contains("/")) {
      QTreeWidgetItem *parent = findItem(files[i].section("/",0,-2));
      QList<QTreeWidgetItem*> list = ui->tree_contents->findItems(files[i].section("/",-3,-2), Qt::MatchExactly, 0);
      if (parent == 0)
        ui->tree_contents->addTopLevelItem(it);
      else
        parent->addChild(it);
    }
    else {
      ui->tree_contents->addTopLevelItem(it);
      QApplication::processEvents();
    }
    changed = true;
  }

  if (changed) {
    QTimer::singleShot (0, this, [this]() {
      ui->tree_contents->resizeColumnToContents(2);
    });
    QTimer::singleShot (0, this, [this]() {
      ui->tree_contents->resizeColumnToContents(1);
    });
  }
  ui->tree_contents->sortItems(0, Qt::AscendingOrder); //sort by name
  ui->tree_contents->sortItems(1, Qt::AscendingOrder); //sort by mimetype (put dirs first - still organized by name)

  setEnabled(true);
  ui->tree_contents->setEnabled(true);
  if (expandAll_){
    ui->tree_contents->expandAll();
    expandAll_ = false;
  }
}

void mainWin::ProcStarting() {
  ui->progressBar->setRange(0,0);
  ui->progressBar->setValue(0);
  ui->progressBar->setVisible(true);
  ui->label_progress->setVisible(!ui->label_progress->text().isEmpty());
  ui->label_progress_icon->setVisible(false);
  ui->tree_contents->setEnabled(false);

  ui->label->setVisible(true);
  ui->label_archive->setVisible(true);
  ui->label_archive->setText(BACKEND->currentFile());
}

void mainWin::ProcFinished(bool success, QString msg) {
  ui->label->setVisible(true);
  ui->label_archive->setVisible(true);
  ui->label_archive->setText(BACKEND->currentFile());

  UpdateTree();
  ui->progressBar->setRange(0, 0);
  ui->progressBar->setValue(0);
  ui->progressBar->setVisible(false);
  ui->label_progress->setText(msg);
  ui->label_progress->setVisible(!msg.isEmpty());
  ui->label_progress_icon->setVisible(!msg.isEmpty());
  if (success)
    ui->label_progress_icon->setPixmap(symbolicIcon::icon(":icons/dialog-ok.svg").pixmap(16, 16));
  else
    ui->label_progress_icon->setPixmap(symbolicIcon::icon(":icons/dialog-error.svg").pixmap(16, 16));
  QFileInfo info(BACKEND->currentFile());
  bool canmodify = info.isWritable();
  if (!info.exists())
    canmodify = QFileInfo(BACKEND->currentFile().section("/", 0, -2)).isWritable();
  canmodify = canmodify && BACKEND->canModify(); //also include the file type limitations
  ui->actionAdd_File->setEnabled(canmodify);
  ui->actionRemove_File->setEnabled(canmodify && info.exists());
  ui->actionExtract_All->setEnabled(info.exists() && ui->tree_contents->topLevelItemCount() > 0);
  ui->actionExtract_Sel->setEnabled(info.exists() && !ui->tree_contents->selectedItems().isEmpty());
  ui->actionAdd_Dirs->setEnabled(canmodify);

  if (close_)
    QTimer::singleShot (500, this, SLOT (close()));
}

void mainWin::ProcUpdate(int percent, QString txt) {
  ui->progressBar->setMaximum(percent < 0 ? 0 : 100);
  ui->progressBar->setValue(percent);
  if (!txt.isEmpty())
    ui->label_progress->setText(txt);
}

void mainWin::selectionChanged() {
  ui->actionExtract_Sel->setEnabled(!ui->tree_contents->selectedItems().isEmpty());
}

}
