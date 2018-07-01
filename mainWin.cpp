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

#include <QMessageBox>
#include <QFileDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QTimer>
#include <QRegularExpression>
#include <QDesktopWidget>

#include <unistd.h> // getuid

namespace Arqiver {

static const QRegularExpression archivingExt("\\.(tar\\.gz|tar\\.xz|tar\\.bz|tar\\.bz2|tar\\.lzma|tar|zip|tgz|txz|tbz|tbz2|tlz|cpio|ar|7z|gz)$");

mainWin::mainWin() : QMainWindow(), ui(new Ui::mainWin) {
  ui->setupUi(this);

  lastPath_ = QDir::homePath();
  expandAll_ = false;
  close_ = false;
  processIsRunning_ = false;

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
  textLabel_->setVisible(false);
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
  connect(ui->actionPassword, &QAction::triggered, [this] {pswrdDialog(true);});
  connect(ui->tree_contents, &QTreeWidget::itemDoubleClicked, this, &mainWin::viewFile);
  connect(ui->tree_contents, &QTreeWidget::itemSelectionChanged, this, &mainWin::selectionChanged);
  connect(ui->tree_contents, &TreeWidget::dragStarted, this, &mainWin::extractSingleFile);
  connect(ui->tree_contents, &QWidget::customContextMenuRequested, this, &mainWin::listContextMenu);

  connect(ui->actionExpand, &QAction::triggered, [this] {ui->tree_contents->expandAll();});
  connect(ui->actionCollapse, &QAction::triggered, [this] {ui->tree_contents->collapseAll();});

  connect (ui->actionAbout, &QAction::triggered, this, &mainWin::aboutDialog);

  /* the labels and column sizes of the header */
  ui->tree_contents->setHeaderLabels(QStringList() << tr("File") << tr("MimeType") << tr("Size"));
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
    QSize ag = QApplication::desktop()->availableGeometry().size();
    if (startSize.width() > ag.width() || startSize.height() > ag.height()) {
      startSize = startSize.boundedTo(ag);
      config_.setStartSize(startSize);
    }
    else if (startSize.isEmpty()) {
      startSize = QSize(600, 500);
      config_.setStartSize(startSize);
    }
    resize(startSize);
  }
  lastFilter_ = config_.getLastFilter();
  if (filterToExtension(lastFilter_).isEmpty()) // validate the filter
    lastFilter_.clear();
}

mainWin::~mainWin() {
  config_.writeConfig();
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

  if (files.isEmpty()) return;
  files.removeDuplicates();
  if (!files.at(0).isEmpty())
    lastPath_ = files.at(0).section("/", 0, -2);

  textLabel_->setText(tr("Opening Archive..."));
  if (action == 0) {
    axFileList_ = files;
    /* here, we don't wait for a successful operation because all files
       should be processed and the window should be closed at the end*/
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
    //QTimer::singleShot(0, this, [this]() {
      newArchive();
    //});
  }
  else {
    expandAll_ = true;
    BACKEND->loadFile(files[0]);
  }
}

QTreeWidgetItem* mainWin::findItem(const QString& path, QTreeWidgetItem *start) {
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

bool mainWin::cleanTree(const QStringList& list) {
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
    supported.insert ("application/x-cd-image", tr("READ-ONLY: ISO image (*.iso *.img)"));
    supported.insert ("application/x-raw-disk-image", tr("READ-ONLY: ISO image (*.iso *.img)"));
    supported.insert ("application/x-xar", tr("READ-ONLY: XAR archive (*.xar)"));
    supported.insert ("application/x-java-archive", tr("READ-ONLY: Java archive (*.jar)"));
    supported.insert ("application/x-rpm", tr("READ-ONLY: RedHat Package (*.rpm)"));
    supported.insert ("application/x-source-rpm", tr("READ-ONLY: RedHat Package (*.rpm)"));
  }
  return supported;
}

QString mainWin::openingTypes() {
  static QString fileTypes;
  if (fileTypes.isEmpty()) {
    QStringList types;
    types << tr("All Known Types %1").arg("(*.tar.gz *.tar.xz *.tar.bz *.tar.bz2 *.bz2 *.tar.lzma *.tar *.zip *.tgz *.txz *.tbz *.tbz2 *.tlz *.cpio *.ar *.7z *.gz *.iso *.img *.xar *.jar *.rpm)");
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

  bool retry(true);
  QString path = saFileList_.isEmpty() ? lastPath_
                                       /* use the file name with simple archiving */
                                       : QFile::exists(saFileList_.at(0)) && QFileInfo(saFileList_.at(0)).isDir()
                                           ? saFileList_.at(0) + (lastFilter_.isEmpty()
                                                                    ? ".tar.gz"
                                                                    : filterToExtension(lastFilter_))
                                           : saFileList_.at(0);
  while (retry) {
    QFileDialog dlg(this, tr("Create Archive"), path, archivingTypes());
    dlg.setAcceptMode(QFileDialog::AcceptSave);
    dlg.setFileMode(QFileDialog::AnyFile);
    dlg.selectNameFilter(lastFilter_);
    if (dlg.exec()) {
      QString f = dlg.selectedNameFilter();
      if (f != allArchivingTypes())
        lastFilter_ = f;
      file = dlg.selectedFiles().at(0);
    }
    else return;
    if (file.isEmpty()) return;
    QRegularExpressionMatch match;
    int indx = file.indexOf(archivingExt, 0, &match);
    if (indx > 0 && indx + match.capturedLength() == file.length())
      retry = false; // the input had an acceptable extension
    else {
      file += (lastFilter_.isEmpty() ? ".tar.gz" : filterToExtension(lastFilter_));
      if (QFile::exists(file)) {
        QMessageBox::StandardButton btn = QMessageBox::question(this,
                                                                tr("Question"),
                                                                tr("The following archive already exists:")
                                                                + QString("\n\n%1\n\n").arg(file)
                                                                + tr("Do you want to replace it?\n"));
        if (btn == QMessageBox::No)
          path = file;
        else retry = false;
      }
      else retry = false;
    }
  }

  lastPath_ = file.section("/", 0, -2);

  textLabel_->setText(""); //just clear it (this action is instant)
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
        lastPswrd_.clear();
        lastPath_ = file.section("/", 0, -2);
        textLabel_->setText(tr("Opening Archive..."));
        expandAll_ = true;
        BACKEND->loadFile(file);
      }
    }
    event->acceptProposedAction();
  }
}

void mainWin::addFiles() {
  /* NOTE: If the archive has list encryption, its
           password will be needed for adding files.
           Otherwise, the password will be optional.*/
  if (lastPswrd_.isEmpty()) {
    if (BACKEND->hasEncryptedList()) {
      if (!pswrdDialog(true, true)) return;
    }
    else
      BACKEND->setPswrd(QString());
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
  textLabel_->setText(tr("Adding Items..."));
  BACKEND->startAdd(files);
}

void mainWin::addDirs() {
  if (lastPswrd_.isEmpty()) { // as in addFiles()
    if (BACKEND->hasEncryptedList()) {
      if (!pswrdDialog(true, true)) return;
    }
    else
      BACKEND->setPswrd(QString());
  }
  QString dirs = QFileDialog::getExistingDirectory(this, tr("Add to Archive"), lastPath_ );
  if (dirs.isEmpty()) return;
  lastPath_ = dirs;
  textLabel_->setText(tr("Adding Items..."));
  BACKEND->startAdd(QStringList() << dirs);
}

void mainWin::removeFiles() {
  QList<QTreeWidgetItem*> sel = ui->tree_contents->selectedItems();
  QStringList items;
  for (int i = 0; i < sel.length(); i++){
    items << sel[i]->whatsThis(0);
  }
  items.removeDuplicates();
  textLabel_->setText(tr("Removing Items..."));
  BACKEND->startRemove(items);
}

void mainWin::extractSingleFile(QTreeWidgetItem *it) {
  if (it->text(1).isEmpty()) return; // it's a directory item
  if (BACKEND->isEncrypted() && BACKEND->getPswrd().isEmpty()
      && BACKEND->isEncryptedPath(it->whatsThis(0))
      && !BACKEND->is7zSingleExtracted(it->whatsThis(0))) {
    if (!pswrdDialog()) return;
  }
  textLabel_->setText(tr("Extracting..."));
  it->setData(0, Qt::UserRole, BACKEND->extractSingleFile(it->whatsThis(0)));
}

void mainWin::listContextMenu(const QPoint& p) {
  QModelIndex index = ui->tree_contents->indexAt(p);
  if (!index.isValid()) return;
  QTreeWidgetItem *item = ui->tree_contents->getItemFromIndex (index);
  if (BACKEND->isDir(item->whatsThis(0)))
    return;

  QMenu menu;
  if (!BACKEND->is7z())
    menu.addAction(ui->actionExtractSel);
  QAction *action = menu.addAction(tr("View Current Item"));
  connect(action, &QAction::triggered, action, [this, item] {viewFile(item);});
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
  }

  return res;
}

void mainWin::extractFiles() {
  if (BACKEND->isEncrypted() && BACKEND->getPswrd().isEmpty()) {
    if (!pswrdDialog()) return;
  }
  QString dir = QFileDialog::getExistingDirectory(this, tr("Extract Into Directory"), lastPath_);
  if (dir.isEmpty()) return;
  lastPath_ = dir;
  textLabel_->setText(tr("Extracting..."));
  BACKEND->startExtract(dir);
}

void mainWin::autoextractFiles() {
  QString dir = BACKEND->currentFile().section("/",0,-2);
  if (dir.isEmpty()) return;
  if (BACKEND->isEncrypted() && BACKEND->getPswrd().isEmpty()) {
    if (!pswrdDialog()) return;
  }
  textLabel_->setText(tr("Extracting..."));
  BACKEND->startExtract(dir);
}

void mainWin::nextAutoExtraction() {
  if (axFileList_.isEmpty()) {
    processIsRunning_ = false;
    QTimer::singleShot (500, this, SLOT (close()));
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
  if (ui->tree_contents->currentItem() == 0) return; // nothing selected

  if (BACKEND->isEncrypted() && BACKEND->getPswrd().isEmpty()) {
    /* not needed because there's no selective extraction for 7z */
    if (!pswrdDialog()) return;
  }

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
  textLabel_->setText(tr("Extracting..."));
  BACKEND->startExtract(dir, selList);
}

void mainWin::viewFile(QTreeWidgetItem *it) {
  if (it->text(1).isEmpty()) return; // it's a directory item
  if (BACKEND->isEncrypted() && BACKEND->getPswrd().isEmpty()
      && BACKEND->isEncryptedPath(it->whatsThis(0))
      && !BACKEND->is7zSingleExtracted(it->whatsThis(0))) {
    if (!pswrdDialog()) return;
  }
  textLabel_->setText(tr("Extracting..."));
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

void mainWin::updateTree() {
  setEnabled(false);
  QStringList files = BACKEND->hierarchy();
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
        it->setText(1, tr("Link To: %1").arg(BACKEND->linkTo(files[i]) ) );
    }
    it->setWhatsThis(0, files[i]);
    if (mime.isEmpty()) {
      it->setIcon(0, QIcon::fromTheme("folder"));
      it->setText(1,""); //clear the mimetype
    }
    else if (BACKEND->isLink(files[i]))
      it->setIcon(0, QIcon::fromTheme("emblem-symbolic-link") );
    else {
      QSize icnSize = ui->tree_contents->iconSize();
      if (icnSize.width() > 16 && BACKEND->isEncryptedPath(it->whatsThis(0))) {
        int pixelRatio = qApp->devicePixelRatio();
        QPixmap icn = QIcon::fromTheme(mime.replace('/', '-')).pixmap(icnSize * pixelRatio);
        QPixmap emblem = symbolicIcon::icon(":icons/emblem-lock.svg").pixmap(16*pixelRatio, 16*pixelRatio);
        QPixmap pix(icnSize * pixelRatio);
        pix.fill(Qt::transparent);
        QPainter painter(&pix);
        painter.drawPixmap(0, 0, icn);
        int offset = (icnSize.width() - 16) * pixelRatio;
        painter.drawPixmap(offset, offset, emblem);
        it->setIcon(0, QIcon(pix));
      }
      else
        it->setIcon(0, QIcon::fromTheme(mime.replace('/', '-')));
    }
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
    QTimer::singleShot(0, this, [this]() {
      ui->tree_contents->resizeColumnToContents(2);
    });
    QTimer::singleShot(0, this, [this]() {
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

void mainWin::procStarting() {
  processIsRunning_ = true;

  statusProgress_->setRange(0, 0);
  statusProgress_->setValue(0);
  statusProgress_->setVisible(true);
  textLabel_->setVisible(!textLabel_->text().isEmpty());
  iconLabel_->setVisible(false);
  ui->tree_contents->setEnabled(false);

  ui->label->setVisible(true);
  ui->label_archive->setVisible(true);
  ui->label_archive->setText(BACKEND->currentFile());
}

void mainWin::procFinished(bool success, const QString& msg) {
  processIsRunning_ = false;

  ui->label->setVisible(true);
  ui->label_archive->setVisible(true);
  ui->label_archive->setText(BACKEND->currentFile());

  updateTree();
  statusProgress_->setRange(0, 0);
  statusProgress_->setValue(0);
  statusProgress_->setVisible(false);
  textLabel_->setText(msg);
  textLabel_->setVisible(!msg.isEmpty());
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
  ui->actionExtractSel->setEnabled(!BACKEND->is7z() // no selective extraction for 7z
                                   && info.exists() && !ui->tree_contents->selectedItems().isEmpty());
  ui->actionAddDir->setEnabled(canmodify && !BACKEND->isGzip());
  ui->actionPassword->setEnabled(BACKEND->is7z());

  if (close_)
    QTimer::singleShot (500, this, SLOT (close()));
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
  ui->actionExtractSel->setEnabled(!BACKEND->is7z() // no selective extraction for 7z
                                   && !ui->tree_contents->selectedItems().isEmpty());
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
