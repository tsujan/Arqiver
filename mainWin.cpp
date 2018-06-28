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
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QRegularExpression>

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
  ui->action_Password->setIcon(symbolicIcon::icon(":icons/locked.svg"));

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
  ui->action_Password->setEnabled(false);

  BACKEND = new Backend(this);
  connect(BACKEND, &Backend::ProcessStarting, this, &mainWin::ProcStarting);
  connect(BACKEND, &Backend::ProcessFinished, this, &mainWin::ProcFinished);
  connect(BACKEND, &Backend::ProgressUpdate, this, &mainWin::ProcUpdate);
  connect(BACKEND, &Backend::encryptedList, this, &mainWin::openEncryptedList);

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
  connect(ui->action_Password, &QAction::triggered, this, &mainWin::pswrdPrompt);
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
  int action = -1;
  /*
     0: auto extracting   -> arqiver --ax Archive
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

  if (files.isEmpty()) return;
  if (!files.at(0).isEmpty())
    lastPath_ = files.at(0).section("/", 0, -2);

  ui->label_progress->setText(tr("Opening Archive..."));
  if (action == 0) {
    connect(BACKEND, &Backend::FileLoaded, this, &mainWin::autoextractFiles);
    connect(BACKEND, &Backend::ExtractSuccessful, [this] {close_ = true;});
    BACKEND->loadFile(files[0]);
  }
  else if (action == 1) {
    aaFileList_ = files;
    aaFileList_.removeFirst();
    connect(BACKEND, &Backend::FileLoaded, this, &mainWin::autoArchiveFiles);
    connect(BACKEND, &Backend::ArchivalSuccessful, [this] {close_ = true;});
    BACKEND->loadFile(files[0]);
  }
  else if (action == 2) {
    connect(BACKEND, &Backend::FileLoaded, this, &mainWin::simpleExtractFiles);
    connect(BACKEND, &Backend::ExtractSuccessful, [this] {close_ = true;});
    BACKEND->loadFile(files[0]);
  }
  else if (action == 3) {
    saFileList_ = files;
    connect(BACKEND, &Backend::FileLoaded, this, &mainWin::simpleArchivetFiles);
    connect(BACKEND, &Backend::ArchivalSuccessful, [this] {close_ = true;});
    //QTimer::singleShot (0, this, [this]() {
      NewArchive();
    //});
  }
  else {
    expandAll_ = true;
    BACKEND->loadFile(files[0]);
  }
}

QTreeWidgetItem* mainWin::findItem(QString path, QTreeWidgetItem *start) {
  if (start == nullptr) {
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
    types << QString(tr("All Types %1")).arg("(*.tar.gz *.tar.xz *.tar.bz *.tar.bz2 *.tar.lzma *.tar *.zip *.tgz *.txz *.tbz *.tbz2 *.tlz *.cpio *.ar *.7z *.gz)");
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
    supported.insert ("application/x-cd-image", tr("READ-ONLY: ISO image (*.iso *.img)"));
    supported.insert ("application/x-raw-disk-image", tr("READ-ONLY: ISO image (*.iso *.img)"));
    supported.insert ("application/x-xar", tr("READ-ONLY: XAR archive (*.xar)"));
    supported.insert ("application/x-java-archive", tr("READ-ONLY: Java archive (*.jar)"));
    supported.insert ("application/x-rpm", tr("READ-ONLY: RedHat Package (*.rpm)"));
    supported.insert ("application/x-source-rpm", tr("READ-ONLY: RedHat Package (*.rpm)"));
  }
  return supported;
}

QString mainWin::OpenFileTypes() {
  static QString fileTypes;
  if (fileTypes.isEmpty()) {
    QStringList types;
    types << QString(tr("All Known Types %1")).arg("(*.tar.gz *.tar.xz *.tar.bz *.tar.bz2 *.bz2 *.tar.lzma *.tar *.zip *.tgz *.txz *.tbz *.tbz2 *.tlz *.cpio *.ar *.7z *.gz *.iso *.img *.xar *.jar *.rpm)");
    QStringList l = supportedMimeTypes().values();
    l.removeDuplicates();
    l.sort();
    types << l;
    types << tr("Show All Files (*)");
    fileTypes = types.join(";;");
  }
  return fileTypes;
}

void mainWin::NewArchive() {
  QString file;

  bool retry(true);
  QString path = saFileList_.isEmpty() ? lastPath_
                                       /* use the file name with simple archiving */
                                       : QFile::exists(saFileList_.at(0)) && QFileInfo(saFileList_.at(0)).isDir()
                                           ? saFileList_.at(0) + ".tar.gz"
                                           : saFileList_.at(0);
  while (retry) {
    QFileDialog dlg(this, tr("Create Archive"), path, CreateFileTypes());
    dlg.setAcceptMode(QFileDialog::AcceptSave);
    dlg.setFileMode(QFileDialog::AnyFile);
    dlg.selectNameFilter(lastFilter_);
    if (dlg.exec()) {
      lastFilter_ = dlg.selectedNameFilter();
      file = dlg.selectedFiles().at(0);
    }
    else return;
    if (file.isEmpty()) return;
    static const QRegularExpression extensions("\\.(tar\\.gz|tar\\.xz|tar\\.bz|tar\\.bz2|tar\\.lzma|tar|zip|tgz|txz|tbz|tbz2|tlz|cpio|ar|7z|gz)$");
    if (file.indexOf(extensions) == -1) {
      QString filter = lastFilter_;
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
  QString file;
  QFileDialog dlg(this, tr("Open Archive"), lastPath_, OpenFileTypes());
  dlg.setAcceptMode(QFileDialog::AcceptOpen);
  dlg.setFileMode(QFileDialog::ExistingFile);
  dlg.selectNameFilter(lastFilter_);
  if (dlg.exec()) {
    lastFilter_ = dlg.selectedNameFilter();
    file = dlg.selectedFiles().at(0);
  }
  if (file.isEmpty()) return;
  lastPath_ = file.section("/", 0, -2);
  ui->label_progress->setText(tr("Opening Archive..."));
  expandAll_ = true;
  BACKEND->loadFile(file);
}

void mainWin::dragEnterEvent(QDragEnterEvent *event) {
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
  if (event->mimeData()->hasFormat ("application/arqiver-item"))
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
  if (BACKEND->isEncrypted() && BACKEND->getPswrd().isEmpty()) {
    if (!pswrdPrompt()) return;
  }
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
  if (BACKEND->isEncrypted() && BACKEND->getPswrd().isEmpty()) {
    if (!pswrdPrompt()) return;
  }
  ui->label_progress->setText(tr("Extracting..."));
  it->setData(0, Qt::UserRole, BACKEND->extractFile(it->whatsThis(0)));
}

bool mainWin::pswrdPrompt() {
  QDialog *dialog = new QDialog (this);
  dialog->setWindowTitle (tr ("Enter Password"));
  QGridLayout *grid = new QGridLayout;
  grid->setSpacing (5);
  grid->setContentsMargins (5, 5, 5, 5);

  QLineEdit *lineEdit = new QLineEdit();
  lineEdit->setMinimumWidth (200);
  lineEdit->setEchoMode (QLineEdit::Password);
  lineEdit->setPlaceholderText ("Enter password");
  connect (lineEdit, &QLineEdit::returnPressed, dialog, &QDialog::accept);
  QLabel *label = new QLabel();
  QSpacerItem *spacer = new QSpacerItem (1, 5);
  QPushButton *cancelButton = new QPushButton (symbolicIcon::icon (":icons/dialog-error.svg"), tr ("Cancel"));
  QPushButton *okButton = new QPushButton (symbolicIcon::icon (":icons/dialog-ok.svg"), tr ("OK"));
  okButton->setDefault(true);
  connect (cancelButton, &QAbstractButton::clicked, dialog, &QDialog::reject);
  connect (okButton, &QAbstractButton::clicked, dialog, &QDialog::accept);

  grid->addWidget (lineEdit, 0, 0, 1, 3);
  grid->addWidget (label, 1, 0, 1, 3);
  grid->addItem (spacer, 2, 0);
  grid->addWidget (cancelButton, 3, 0, 1, 2, Qt::AlignRight);
  grid->addWidget (okButton, 3, 2, Qt::AlignCenter);
  grid->setColumnStretch (1, 1);
  grid->setRowStretch (2, 1);
  label->setVisible (false);

  dialog->setLayout (grid);

  bool res = true;
  switch (dialog->exec()) {
  case QDialog::Accepted:
    if (lineEdit->text().isEmpty())
      res = false;
    else
      BACKEND->setPswrd(lineEdit->text());
    delete dialog;
    break;
  case QDialog::Rejected:
  default:
    delete dialog;
    res = false;
    break;
  }
  return res;
}

void mainWin::extractFiles() {
  if (BACKEND->isEncrypted() && BACKEND->getPswrd().isEmpty()) {
    if (!pswrdPrompt()) return;
  }
  QString dir = QFileDialog::getExistingDirectory(this, tr("Extract Into Directory"), lastPath_);
  if (dir.isEmpty()) return;
  lastPath_ = dir;
  ui->label_progress->setText(tr("Extracting..."));
  BACKEND->startExtract(dir);
}

void mainWin::autoextractFiles() {
  disconnect(BACKEND, &Backend::FileLoaded, this, &mainWin::autoextractFiles);
  QString dir = BACKEND->currentFile().section("/",0,-2);
  if (dir.isEmpty()) return;
  if (BACKEND->isEncrypted() && BACKEND->getPswrd().isEmpty()) {
    if (!pswrdPrompt()) return;
  }
  ui->label_progress->setText(tr("Extracting..."));
  BACKEND->startExtract(dir);
}

void mainWin::simpleExtractFiles() {
  disconnect(BACKEND, &Backend::FileLoaded, this, &mainWin::simpleExtractFiles);
  extractFiles();
}

void mainWin::autoArchiveFiles() { // no protection against overwriting
  disconnect(BACKEND, &Backend::FileLoaded, this, &mainWin::autoArchiveFiles);
  ui->label_progress->setText(tr("Adding Items..."));
  BACKEND->startAdd(aaFileList_);
}

void mainWin::simpleArchivetFiles() {
  disconnect(BACKEND, &Backend::FileLoaded, this, &mainWin::simpleArchivetFiles);
  ui->label_progress->setText(tr("Adding Items..."));
  BACKEND->startAdd(saFileList_);
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
  BACKEND->startExtract(dir, selList);
}

void mainWin::ViewFile(QTreeWidgetItem *it) {
  if (it->text(1).isEmpty()) return; // it's a directory item
  if (BACKEND->isEncrypted() && BACKEND->getPswrd().isEmpty()) {
    if (!pswrdPrompt()) return;
  }
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
      mime = BACKEND->getMimeType(files[i].section("/",-1));
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
  //ui->label_progress_icon->setVisible(false);
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
  ui->label_progress_icon->setVisible(true);
  if (success) {
    if (BACKEND->isEncrypted()) {
      if (BACKEND->getPswrd().isEmpty())
        ui->label_progress_icon->setPixmap(symbolicIcon::icon(":icons/locked.svg").pixmap(16, 16));
      else
        ui->label_progress_icon->setPixmap(symbolicIcon::icon(":icons/unlocked.svg").pixmap(16, 16));
    }
    else
      ui->label_progress_icon->setPixmap(symbolicIcon::icon(":icons/dialog-ok.svg").pixmap(16, 16));
  }
  else
    ui->label_progress_icon->setPixmap(symbolicIcon::icon(":icons/dialog-error.svg").pixmap(16, 16));
  QFileInfo info(BACKEND->currentFile());
  bool canmodify = info.isWritable();
  if (!info.exists())
    canmodify = QFileInfo(BACKEND->currentFile().section("/", 0, -2)).isWritable();
  canmodify = canmodify && BACKEND->canModify(); // also include the file type limitations
  ui->actionAdd_File->setEnabled(canmodify && (!BACKEND->isGzip() || ui->tree_contents->topLevelItemCount() == 0));
  ui->actionRemove_File->setEnabled(canmodify && info.exists() && !BACKEND->isGzip());
  ui->actionExtract_All->setEnabled(info.exists() && ui->tree_contents->topLevelItemCount() > 0);
  ui->actionExtract_Sel->setEnabled(info.exists() && !ui->tree_contents->selectedItems().isEmpty());
  ui->actionAdd_Dirs->setEnabled(canmodify && !BACKEND->isGzip());
  ui->action_Password->setEnabled(BACKEND->is7z() && BACKEND->heirarchy().isEmpty());

  if (close_)
    QTimer::singleShot (500, this, SLOT (close()));
}

void mainWin::ProcUpdate(int percent, QString txt) {
  ui->progressBar->setMaximum(percent < 0 ? 0 : 100);
  ui->progressBar->setValue(percent);
  if (!txt.isEmpty())
    ui->label_progress->setText(txt);
}

void mainWin::openEncryptedList(const QString& path) {
  if (!pswrdPrompt()) return;
  BACKEND->loadFile(path, true);
}

void mainWin::selectionChanged() {
  ui->actionExtract_Sel->setEnabled(!ui->tree_contents->selectedItems().isEmpty());
}

}
