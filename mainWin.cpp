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

#include "mainWin.h"
#include "ui_mainWin.h"
#include "ui_about.h"
#include "svgicons.h"
#include "pref.h"

#include <QUrl>
#include <QMessageBox>
#include <QFileDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QScrollBar>
#include <QMimeData>
#include <QRegularExpression>
//#include <QScreen>
#include <QDesktopServices>
#include <QPixmapCache>
#include <QClipboard>
#include <QResizeEvent>
#include <QPointer>
#include <QDrag>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QStandardPaths>

#include <unistd.h> // getuid
#include <algorithm>
#include <cmath>

namespace Arqiver {

static const QRegularExpression archivingExt("\\.(tar\\.gz|tar\\.xz|tar\\.bz|tar\\.bz2|tar\\.lzma|tar\\.zst|tar|zip|tgz|txz|tzst|tbz|tbz2|tlz|cpio|ar|7z|gz)$");
/*************************/
QVariant TreeWidgetItem::data(int column, int role) const {
  if (backend_ != nullptr
      && column == 0
      && role == Qt::DecorationRole && iconSize_.width() > 16
      // not a folder
      && columnCount() > 1 && !text(1).isEmpty()) {
    if (columnCount() > 3 && data(3, Qt::UserRole).toString() == "lock") // encrypted path
      return emblemizedPixmap(text(1).replace('/', '-'), iconSize_, isSelected(), true);
    if (columnCount() > 2 && text(2).isEmpty()) { // a link
      auto file = whatsThis(0);
      if (!file.isEmpty()) {
        auto mime = backend_->getMimeType(file.section("/", -1));
        if (!mime.isEmpty())
          return emblemizedPixmap(mime.replace('/', '-'), iconSize_, isSelected(), false);
      }
    }
  }
  return QTreeWidgetItem::data(column, role);
}

bool TreeWidgetItem::operator<(const QTreeWidgetItem& other) const {
  /* treat dot as a separator for a more natural sorting */
  int column = treeWidget() ? treeWidget()->sortColumn() : 0;
  const QString txt1 = text(column);
  const QString txt2 = other.text(column);
  int start1 = 0, start2 = 0;
  for (;;) {
    int end1 = txt1.indexOf(QLatin1Char('.'), start1);
    int end2 = txt2.indexOf(QLatin1Char('.'), start2);
    QString part1 = end1 == -1 ? txt1.sliced(start1, txt1.size() - start1)
                               : txt1.sliced(start1, end1 - start1);
    QString part2 = end2 == -1 ? txt2.sliced(start2, txt2.size() - start2)
                               : txt2.sliced(start2, end2 - start2);
    int comp = collator_.compare(part1, part2);
    if (comp == 0)
      comp = part1.size() - part2.size(); // a workaround for QCollator's bug
    if (comp != 0)
      return comp < 0;
    if (end1 == -1 || end2 == -1)
      return end1 < end2;
    start1 = end1 + 1;
    start2 = end2 + 1;
  }
}

QPixmap TreeWidgetItem::emblemizedPixmap(const QString iconName, const QSize& icnSize,
                                         bool selected, bool lock) const {
  QString emblemName = lock ? ".lock" : ".link";
  QString key = iconName + emblemName + (selected ? "1" : "0");
  QPixmap pix;
  if (!QPixmapCache::find(key, &pix)) {
    QPixmap icn = QIcon::fromTheme(iconName, symbolicIcon::icon(":icons/unknown.svg"))
                  .pixmap(icnSize, selected ? QIcon::Selected : QIcon::Normal);
    int w = icnSize.width();
    int emblemSize = w <= 32 ? 16 : 24;
    int offset = 0;
    QPixmap emblem;
    if (lock) {
      emblem = QIcon(":icons/emblem-lock.svg").pixmap(emblemSize, emblemSize);
      offset = w - emblemSize;
    }
    else
      emblem = QIcon(":icons/emblem-symbolic-link.svg").pixmap(emblemSize, emblemSize);
    double pixelRatio = icn.devicePixelRatio();
    pix = QPixmap((QSizeF(icnSize) * pixelRatio).toSize());
    pix.fill(Qt::transparent);
    pix.setDevicePixelRatio(pixelRatio);
    QPainter painter(&pix);
    painter.drawPixmap(0, 0, icn);
    painter.drawPixmap(offset, offset, emblem);
    QPixmapCache::insert(key, pix);
  }
  return pix;
}
/*************************/
mainWin::mainWin() : QMainWindow(), ui(new Ui::mainWin) {
  ui->setupUi(this);

  lastPath_ = QDir::homePath();
  canModify_ = true;
  canUpdate_ = true;
  updateTree_ = true; // will be set to false when extracting (or viewing)
  scrollToCurrent_ = true; // will be set to false when adding files/folders
  expandAll_ = false;
  close_ = false;
  processIsRunning_ = false;
  filterTimer_ = nullptr;
  reapplyFilter_ = true;

  ui->tree_contents->installEventFilter(this);
  ui->tree_contents->viewport()->installEventFilter(this);

  if (getuid() == 0) {
    setWindowTitle("Arqiver (" + tr("Root") + ")");
    isRoot_ = true;
  }
  else isRoot_ = false;

  /* status bar */
  iconLabel_ = new QLabel();
  iconLabel_->setFixedSize(16, 16);
  iconLabel_->setFocusPolicy(Qt::NoFocus);
  textLabel_ = new Label();
  textLabel_->setFocusPolicy(Qt::NoFocus);
  statusProgress_ = new QProgressBar();
  statusProgress_->setTextVisible(false);
  statusProgress_->setMaximumHeight(std::max(QFontMetrics(font()).height(), 16));
  statusProgress_->setFocusPolicy(Qt::NoFocus);
  statusProgress_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
  ui->statusbar->addWidget(iconLabel_);
  ui->statusbar->addWidget(statusProgress_);
  ui->statusbar->addPermanentWidget(textLabel_);
  iconLabel_->setVisible(false);
  statusProgress_->setVisible(false);

  config_.readConfig();

  /* icons */
  setWindowIcon(QIcon::fromTheme("arqiver", QIcon(":icons/arqiver.svg")));
  if (config_.getSysIcons()) {
    ui->actionNew->setIcon(QIcon::fromTheme("document-new",
                                            symbolicIcon::icon(":icons/document-new.svg")));
    ui->actionOpen->setIcon(QIcon::fromTheme("document-open",
                                             symbolicIcon::icon(":icons/document-open.svg")));
    ui->actionQuit->setIcon(QIcon::fromTheme("application-exit",
                                             symbolicIcon::icon(":icons/application-exit.svg")));
    ui->actionUpdate->setIcon(QIcon::fromTheme("software-update-urgent",
                                               symbolicIcon::icon(":icons/update.svg")));
    ui->actionAddFile->setIcon(QIcon::fromTheme("archive-insert",
                                                symbolicIcon::icon(":icons/archive-insert.svg")));
    ui->actionAddDir->setIcon(QIcon::fromTheme("archive-insert-directory",
                                               symbolicIcon::icon(":icons/archive-insert-directory.svg")));
    ui->actionRemoveFile->setIcon(QIcon::fromTheme("archive-remove",
                                                   symbolicIcon::icon(":icons/archive-remove.svg")));
    ui->actionExtractAll->setIcon(QIcon::fromTheme("archive-extract",
                                                   symbolicIcon::icon(":icons/archive-extract.svg")));
    ui->actionExtractSel->setIcon(QIcon::fromTheme("edit-select-all",
                                                   symbolicIcon::icon(":icons/edit-select-all.svg")));
    ui->actionView->setIcon(QIcon::fromTheme("object-visible",
                                             symbolicIcon::icon(":icons/view.svg")));
    ui->actionPassword->setIcon(QIcon::fromTheme("object-locked",
                                                 symbolicIcon::icon(":icons/locked.svg")));
    ui->actionExpand->setIcon(QIcon::fromTheme("go-down",
                                               symbolicIcon::icon(":icons/expand.svg")));
    ui->actionCollapse->setIcon(QIcon::fromTheme("go-up",
                                                 symbolicIcon::icon(":icons/collapse.svg")));
    ui->actionAbout->setIcon(QIcon::fromTheme("help-about",
                                              symbolicIcon::icon(":icons/help-about.svg")));
    ui->actionCopy->setIcon(QIcon::fromTheme("edit-copy",
                                             symbolicIcon::icon(":icons/edit-copy.svg")));
    ui->actionPref->setIcon(QIcon::fromTheme("preferences-system",
                                             symbolicIcon::icon(":icons/preferences-system.svg")));
    ui->actionStop->setIcon(QIcon::fromTheme("process-stop",
                                             symbolicIcon::icon(":icons/process-stop.svg")));
  }
  else {
    ui->actionNew->setIcon(symbolicIcon::icon(":icons/document-new.svg"));
    ui->actionOpen->setIcon(symbolicIcon::icon(":icons/document-open.svg"));
    ui->actionQuit->setIcon(symbolicIcon::icon(":icons/application-exit.svg"));
    ui->actionUpdate->setIcon(symbolicIcon::icon(":icons/update.svg"));
    ui->actionAddFile->setIcon(symbolicIcon::icon(":icons/archive-insert.svg"));
    ui->actionAddDir->setIcon(symbolicIcon::icon(":icons/archive-insert-directory.svg"));
    ui->actionRemoveFile->setIcon(symbolicIcon::icon(":icons/archive-remove.svg"));
    ui->actionExtractAll->setIcon(symbolicIcon::icon(":icons/archive-extract.svg"));
    ui->actionExtractSel->setIcon(symbolicIcon::icon(":icons/edit-select-all.svg"));
    ui->actionView->setIcon(symbolicIcon::icon(":icons/view.svg"));
    ui->actionPassword->setIcon(symbolicIcon::icon(":icons/locked.svg"));
    ui->actionExpand->setIcon(symbolicIcon::icon(":icons/expand.svg"));
    ui->actionCollapse->setIcon(symbolicIcon::icon(":icons/collapse.svg"));
    ui->actionAbout->setIcon(symbolicIcon::icon(":icons/help-about.svg"));
    ui->actionCopy->setIcon(symbolicIcon::icon(":icons/edit-copy.svg"));
    ui->actionPref->setIcon(symbolicIcon::icon(":icons/preferences-system.svg"));
    ui->actionStop->setIcon(symbolicIcon::icon(":icons/process-stop.svg"));
  }

  ui->label->setVisible(false);
  ui->label_archive->setVisible(false);

  ui->frame->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(ui->frame, &QWidget::customContextMenuRequested, this, &mainWin::labelContextMenu);

  ui->actionUpdate->setEnabled(false);
  ui->actionUpdate->setVisible(false);

  ui->actionAddFile->setEnabled(false);
  ui->actionRemoveFile->setEnabled(false);
  ui->actionExtractAll->setEnabled(false);
  ui->actionAddDir->setEnabled(false);
  ui->actionExtractSel->setEnabled(false);
  ui->actionView->setEnabled(false);
  ui->actionPassword->setEnabled(false);

  BACKEND = new Backend(this);
  connect(BACKEND, &Backend::processStarting, this, &mainWin::procStarting);
  connect(BACKEND, &Backend::processFinished, this, &mainWin::procFinished);
  connect(BACKEND, &Backend::progressUpdate, this, &mainWin::procUpdate);
  connect(BACKEND, &Backend::encryptedList, this, &mainWin::openEncryptedList);
  connect(BACKEND, &Backend::errorMsg, this, [this](const QString& msg) {
    QMessageBox::critical(this, tr("Error"), msg);
  });
  connect(BACKEND, &Backend::fileModified, this, [this](bool modified) {
    bool reallyModified(modified && canUpdate_);
    ui->actionUpdate->setEnabled(reallyModified);
    setWindowModified(reallyModified);
  });
  connect(ui->actionStop, &QAction::triggered, [this] {BACKEND->killProc();});

  QWidget *spacer = new QWidget(this);
  spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  ui->toolBar->insertWidget(ui->actionUpdate, spacer);

  connect(ui->actionNew, &QAction::triggered, this, &mainWin::newArchive);
  connect(ui->actionOpen, &QAction::triggered, this, &mainWin::openArchive);
  connect(ui->actionQuit, &QAction::triggered, this, &mainWin::close);
  connect(ui->actionUpdate, &QAction::triggered, BACKEND, &Backend::updateArchive);
  connect(ui->actionAddFile, &QAction::triggered, this, &mainWin::addFiles);
  connect(ui->actionRemoveFile, &QAction::triggered, this, &mainWin::removeFiles);
  connect(ui->actionExtractAll, &QAction::triggered, this, &mainWin::extractFiles);
  connect(ui->actionExtractSel, &QAction::triggered, this, &mainWin::extractSelection);
  connect(ui->actionView, &QAction::triggered, [this] {
    if (QTreeWidgetItem *cur = ui->tree_contents->currentItem())
      viewFile(cur);
  });
  connect(ui->actionAddDir, &QAction::triggered, this, &mainWin::addDirs);
  connect(ui->actionCopy, &QAction::triggered, [this] {
    if (QTreeWidgetItem *cur = ui->tree_contents->currentItem())
      QApplication::clipboard()->setText(cur->whatsThis(0));
  });
  connect(ui->actionPassword, &QAction::triggered, [this] {pswrdDialog(true);});
  connect(ui->tree_contents, &QTreeWidget::itemDoubleClicked, this, &mainWin::viewFile);
  connect(ui->tree_contents, &TreeWidget::enterPressed, this, &mainWin::onEnterPressed);
  connect(ui->tree_contents, &QTreeWidget::itemSelectionChanged, this, &mainWin::selectionChanged);
  connect(ui->tree_contents, &QTreeWidget::currentItemChanged,
          [this] (QTreeWidgetItem *cur, QTreeWidgetItem*) {
    ui->actionView->setEnabled(cur && cur->isSelected() && !cur->text(1).isEmpty());
  });
  connect(ui->tree_contents, &TreeWidget::dragStarted, this, &mainWin::extractDraggedItems);
  connect(ui->tree_contents, &QWidget::customContextMenuRequested, this, &mainWin::listContextMenu);
  connect(ui->tree_contents, &QTreeWidget::itemExpanded, this, &mainWin::onChangingExpansion);
  connect(ui->tree_contents, &QTreeWidget::itemCollapsed, this, &mainWin::onChangingExpansion);

  connect(ui->actionExpand, &QAction::triggered, [this] {
    /* WARNING: Contrary to what Qt doc says, QTreeWidget::itemExpanded() is called with expandAll(). */
    disconnect(ui->tree_contents, &QTreeWidget::itemCollapsed, this, &mainWin::onChangingExpansion);
    disconnect(ui->tree_contents, &QTreeWidget::itemExpanded, this, &mainWin::onChangingExpansion);
    ui->tree_contents->expandAll();
    ui->tree_contents->scrollTo(ui->tree_contents->currentIndex());
    adjustColumnSizes(); /* may not be called by eventFilter()
                            because scrollbars may be transient */
    connect(ui->tree_contents, &QTreeWidget::itemExpanded, this, &mainWin::onChangingExpansion);
    connect(ui->tree_contents, &QTreeWidget::itemCollapsed, this, &mainWin::onChangingExpansion);
  });
  connect(ui->actionCollapse, &QAction::triggered, [this] {
    disconnect(ui->tree_contents, &QTreeWidget::itemCollapsed, this, &mainWin::onChangingExpansion);
    disconnect(ui->tree_contents, &QTreeWidget::itemExpanded, this, &mainWin::onChangingExpansion);
    ui->tree_contents->collapseAll();
    adjustColumnSizes();
    connect(ui->tree_contents, &QTreeWidget::itemExpanded, this, &mainWin::onChangingExpansion);
    connect(ui->tree_contents, &QTreeWidget::itemCollapsed, this, &mainWin::onChangingExpansion);
  });

  connect(ui->lineEdit, &QLineEdit::textChanged, this, &mainWin::filter);

  connect(ui->actionPref, &QAction::triggered, this, &mainWin::prefDialog);

  connect(ui->actionAbout, &QAction::triggered, this, &mainWin::aboutDialog);

  /* the labels and column sizes of the header (the 4th hidden column will
     be used for putting directory items first and will save the lock info) */
  ui->tree_contents->setHeaderLabels(QStringList() << tr("File") << tr("MimeType") << tr("Size") << QString());
  ui->tree_contents->header()->setSectionHidden(3, true);
  ui->tree_contents->header()->setVisible(false); // will be shown in procFinished()

  /* support file dropping into the window */
  setAcceptDrops(true);

  /* apply the configuration */
  adjustColumnSizes();
  BACKEND->setTarCommand(config_.getTarBinary());
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
      startSize = QSize(700, 500);
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
  delete ui;
}

bool mainWin::eventFilter(QObject *watched, QEvent *event) {
  if (watched == ui->tree_contents->viewport()) {
    if (event->type() == QEvent::Resize) {
      if (QResizeEvent *re = static_cast<QResizeEvent*>(event)) {
        if (re->size().width() != re->oldSize().width())
          adjustColumnSizes(); // doesn't affect the viewport's width
      }
    }
  }
  else if (watched == ui->tree_contents && event->type() == QEvent::KeyPress) {
    if (QKeyEvent *ke = static_cast<QKeyEvent*>(event)) {
      /* select/deselect the current item with Ctrl+Space and also
         select it if it isn't selected and the first Space is typed */
      QTreeWidgetItem *cur = ui->tree_contents->currentItem();
      if (cur && ke->key() == Qt::Key_Space
          && (ke->modifiers() == Qt::ControlModifier
              || (ui->lineEdit->text().isEmpty() // first typed
                  && !ui->tree_contents->selectedItems().contains(cur)))) {
        QModelIndex curIndex = ui->tree_contents->getIndexFromItem(cur);
        ui->tree_contents->selectionModel()
          ->setCurrentIndex(curIndex,
                            QItemSelectionModel::Toggle | QItemSelectionModel::Rows);
        ui->tree_contents->scrollTo(curIndex);
        return false;
      }
      /* when a text is typed inside the tree, type it inside the filter line-edit */
      if (ke->key() != Qt::Key_Return && ke->key() != Qt::Key_Enter
          && ke->key() != Qt::Key_Up && ke->key() != Qt::Key_Down
          && ke->key() != Qt::Key_Home && ke->key() != Qt::Key_End
          && ke->key() != Qt::Key_PageUp && ke->key() != Qt::Key_PageDown
          && !(ke->modifiers() == Qt::ControlModifier && ke->key() == Qt::Key_A)) {
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
  }
  else {
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
  ui->tree_contents->scrollTo(ui->tree_contents->currentIndex());
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
      item->setHidden(false);
      return;
    }
  }
  item->setHidden(!item->text(0).contains(ui->lineEdit->text(), Qt::CaseInsensitive));
}

bool mainWin::ignoreChanges() {
  if (QMessageBox::question(this, tr("Question"),
                            tr("Some files have been modified.")
                              + "\n" + tr("Do you want to ignore the changes?\n"),
                            QMessageBox::Yes | QMessageBox::No,
                            QMessageBox::No)
      == QMessageBox::No) {
    return false;
  }
  return true;
}

void mainWin::closeEvent(QCloseEvent *event) {
  if (processIsRunning_)
    event->ignore();
  else {
    if (ui->actionUpdate->isEnabled()) {
      if (!ignoreChanges()) {
        event->ignore();
        return;
      }
    }

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
  /* KDE needs all events to be processed; otherwise, if a dialog
     is shown before the main window, the application won't exit
     when the main window is closed. This should be a bug in KDE.
     As a workaround, we show the window only when no dialog is
     going to be shown before it. */
  QTimer::singleShot(0, this, [this, args]() {
    int action = -1; // load archive
    /*
      0: auto extracting   -> arqiver --ax Archive(s)
      1: auto archiving    -> arqiver --aa Archive Files
      2: simple extracting -> arqiver --sx Archive
      3: simple archiving  -> arqiver --sa Files
    */
    if (!args.isEmpty()) {
      if (args.at(0) == "--ax") {
        action = 0;
      }
      else if (args.at(0) == "--aa") {
        action = 1;
      }
      else if (args.at(0) == "--sx") {
        action = 2;
      }
      else if (args.at(0) == "--sa") {
        action = 3;
      }
    }
    QStringList files;
    int i = (action >= 0 ? 1 : 0);
    for (; i < args.length(); i++) {
      if (!args.at(i).isEmpty()) {
        /* always an absolute path */
        QString file = args.at(i);
        if (file.startsWith("file://"))
          file = QUrl(file).toLocalFile();
        file = QDir::current().absoluteFilePath(file);
        files << QDir::cleanPath(file);
      }
    }

    if (action != 3) // no dialog is shown before the main window
      show();

    if (files.isEmpty()) return;
    files.removeDuplicates();
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
      BACKEND->loadFile(files.at(0));
    }
    else if (action == 2) {
      connect(BACKEND, &Backend::loadingSuccessful, this, &mainWin::simpleExtractFiles);
      connect(BACKEND, &Backend::extractionSuccessful, [this] {close_ = true;});
      BACKEND->loadFile(files.at(0));
    }
    else if (action == 3) {
      saFileList_ = files;
      connect(BACKEND, &Backend::loadingSuccessful, this, &mainWin::simpleArchivetFiles);
      connect(BACKEND, &Backend::archivingSuccessful, [this] {close_ = true;});
      newArchive();
    }
    else {
      expandAll_ = true;
      BACKEND->loadFile(files.at(0));
    }
  });
}

QHash<QString, QTreeWidgetItem*> mainWin::cleanTree(const QStringList& list) {
  /* this is a workaround for a Qt bug that might give a too small height to rows
     if the horizontal scrollbar isn't at its minimum value when items are removed */
  if (QScrollBar *hs = ui->tree_contents->horizontalScrollBar()) {
    if (hs->isVisible())
      hs->setValue(hs->minimum());
  }

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
  static const QString allTypes = tr("All Types") + " (*.tar.gz *.tar.xz *.tar.bz *.tar.bz2 *.tar.lzma *.tar.zst *.tar *.zip *.tgz *.txz *.tzst *.tbz *.tbz2 *.tlz *.cpio *.ar *.7z *.gz)";
  return allTypes;
}

QString mainWin::archivingTypes() {
  static QString fileTypes;
  if (fileTypes.isEmpty()) {
    QStringList types;
    types << allArchivingTypes();
    QStringList tmp;
    tmp << tr("Uncompressed Archive") + " (*.tar)";
    tmp << tr("GZip Compressed Archive") + " (*.tar.gz *.tgz)";
    tmp << tr("BZip Compressed Archive") + " (*.tar.bz *.tbz)";
    tmp << tr("BZip2 Compressed Archive") + " (*.tar.bz2 *.tbz2)";
    tmp << tr("LMZA Compressed Archive") + " (*.tar.lzma *.tlz)";
    tmp << tr("XZ Compressed Archive") + " (*.tar.xz *.txz)";
    tmp << tr("Zstandard Compressed Archive") + " (*.tar.zst *.tzst)";
    tmp << tr("CPIO Archive") + " (*.cpio)";
    //types << tr("PAX Archive") + " (*.pax)";
    tmp << tr("AR Archive") + " (*.ar)";
    //tmp << tr("SHAR Archive") + " (*.shar)";
    tmp << tr("Zip Archive") + " (*.zip)";
    tmp << tr("Gzip Archive") + " (*.gz)";
    tmp << tr("7-Zip Archive") + " (*.7z)";
    tmp.sort();
    types << tmp;
    fileTypes = types.join(";;");
  }
  return fileTypes;
}

QHash<QString, QString> mainWin::supportedMimeTypes() {
  static QHash<QString, QString> supported;
  if (supported.isEmpty()) {
    supported.insert ("application/x-tar", tr("Uncompressed Archive") + " (*.tar)");
    supported.insert ("application/x-compressed-tar", tr("GZip Compressed Archive") + " (*.tar.gz *.tgz)");
    supported.insert ("application/x-bzip-compressed-tar", tr("BZip Compressed Archive") + " (*.tar.bz *.tbz)");
    supported.insert ("application/x-bzip1-compressed-tar", tr("BZip Compressed Archive") + " (*.tar.bz *.tbz)");
    supported.insert ("application/x-bzip2-compressed-tar", tr("BZip2 Compressed Archive") + " (*.tar.bz2 *.tbz2)");
    supported.insert ("application/x-bzip", tr("READ-ONLY: BZip Archive") + " (*.bz)");
    supported.insert ("application/x-bzip1", tr("READ-ONLY: BZip Archive") + " (*.bz)");
    supported.insert ("application/x-bzip2", tr("READ-ONLY: BZip2 Archive") + " (*.bz2)");
    supported.insert ("application/x-bzpdf", tr("READ-ONLY: BZip2 Compressed PDF Document") + " (*.pdf.bz2)");
    supported.insert ("application/x-xz-compressed-tar", tr("XZ Compressed Archive") + " (*.tar.xz *.txz)");
    supported.insert ("application/x-xz", tr("READ-ONLY: XZ archive") + " (*.xz)");
    supported.insert ("application/x-xzpdf", tr("READ-ONLY: XZ Compressed PDF Document") + " (*.pdf.xz)");
    supported.insert ("application/x-lzma-compressed-tar", tr("LMZA Compressed Archive") + " (*.tar.lzma *.tlz)");
    supported.insert ("application/x-zstd-compressed-tar", tr("Zstandard Compressed Archive") + " (*.tar.zst *.tzst)");
    supported.insert ("application/zstd", tr("READ-ONLY: Zstandard archive") + " (*.zst)");
    supported.insert ("application/x-cpio", tr("CPIO Archive") + " (*.cpio)");
    //supported.insert ("?", tr("PAX Archive") + " (*.pax)");
    supported.insert ("application/x-archive", tr("AR Archive") + " (*.ar)");
    //supported.insert ("application/x-shar", tr("SHAR Archive") + " (*.shar)");
    supported.insert ("application/zip", tr("Zip Archive") + " (*.zip)");
    supported.insert ("application/x-7z-compressed", tr("7-Zip Archive") + " (*.7z)");
    supported.insert ("application/gzip", tr("Gzip Archive") + " (*.gz)");
    supported.insert ("application/x-gzpdf", tr("Gzip Compressed PDF Document") + " (*.pdf.gz)");
    supported.insert ("image/svg+xml-compressed", tr("READ-ONLY: Compressed SVG Image") + " (*.svgz)");
    supported.insert ("application/x-cd-image", tr("READ-ONLY: ISO Image") + " (*.iso *.img)");
    supported.insert ("application/vnd.efi.iso", tr("READ-ONLY: ISO Image") + " (*.iso *.img)");
    supported.insert ("application/x-raw-disk-image", tr("READ-ONLY: ISO Image") + " (*.iso *.img)");
    supported.insert ("application/vnd.efi.img", tr("READ-ONLY: ISO Image") + " (*.iso *.img)");
    supported.insert ("application/x-xar", tr("READ-ONLY: XAR Archive") + " (*.xar)");
    supported.insert ("application/x-java-archive", tr("READ-ONLY: Java Archive") + " (*.jar)");
    supported.insert ("application/vnd.debian.binary-package", tr("READ-ONLY: Debian Package") + " (*.deb)");
    supported.insert ("application/x-rpm", tr("READ-ONLY: RedHat Package") + " (*.rpm)");
    supported.insert ("application/x-source-rpm", tr("READ-ONLY: RedHat Package") + " (*.rpm)");
    supported.insert ("application/x-ms-dos-executable", tr("READ-ONLY: MS Windows Executable") + " (*.exe *.com)");
    supported.insert ("application/vnd.microsoft.portable-executable", tr("READ-ONLY: MS Windows Executable") + " (*.exe *.com)");
    supported.insert ("application/x-msdownload", tr("READ-ONLY: MS Windows Executable") + " (*.exe *.com)");
    supported.insert ("application/x-msi", tr("READ-ONLY: MS Windows Installer Package") + " (*.msi)");
    supported.insert ("application/vnd.ms-cab-compressed", tr("READ-ONLY: MS Windows Cabinet Archive") + " (*.cab)");
    //supported.insert ("application/x-ace", tr("READ-ONLY: ACE archive") + " (*.ace)");
    supported.insert ("application/vnd.android.package-archive", tr("READ-ONLY: Android Package") + " (*.apk)");
    supported.insert ("application/vnd.rar", tr("READ-ONLY: RAR Archive") + " (*.rar)");
    supported.insert ("application/vnd.appimage", tr("READ-ONLY: AppImage application bundle") + " (*.appimage)");
    supported.insert ("application/x-virtualbox-vbox-extpack", tr("READ-ONLY: VirtualBox Extension Pack") + " (*.vbox-extpack)");
  }
  return supported;
}

QString mainWin::openingTypes() {
  static QString fileTypes;
  if (fileTypes.isEmpty()) {
    QStringList types;
    types << tr("All Known Types") + " (*.tar.gz *.tar.xz *.xz *.tar.bz *.tar.bz2 *.bz *.bz2 *.tar.lzma *.tar.zst *.zst *.tar *.zip *.tgz *.txz *.tzst *.tbz *.tbz2 *.tlz *.cpio *.ar *.7z *.gz *.svgz *.iso *.img *.xar *.jar *.deb *.rpm *.exe *.com *.msi *.cab *.apk *.rar *.appimage *.vbox-extpack *.dmg)";
    QStringList l = supportedMimeTypes().values();
    l.removeDuplicates();
    l.sort();
    types << l;
    types << tr("Show All Files") + " (*)";
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
                                                                + tr("Do you want to replace it?\n"),
                                                                QMessageBox::Yes | QMessageBox::No,
                                                                QMessageBox::No);
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
  if (ui->actionUpdate->isEnabled()) {
    if (!ignoreChanges())
      return;
  }

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
  if (BACKEND->isWorking() // no DND when the backend is busy
      || event->source()) {
    // if the drag has originated in this window, ignore it
    // (needed when an archive contains archives, as with deb packages)
    event->ignore();
    return;
  }
  static const QStringList unadvertisedMimeTypes = {"application/x-apple-diskimage",
                                                    "application/vnd.snap"};
  if (event->mimeData()->hasUrls()) {
    const QList<QUrl> urlList = event->mimeData()->urls();
    if (!urlList.isEmpty()) {
      QString file = urlList.at(0).toLocalFile();
      if (!file.isEmpty()) {
        QString mime = BACKEND->getMimeType(file);
        if (!supportedMimeTypes().contains(mime)
            && !unadvertisedMimeTypes.contains(mime)) {
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
  if (BACKEND->isWorking()) {
    event->ignore();
    return;
  }
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
  if (BACKEND->isGzip()) { // accepts only one file
    QFileDialog dialog(this);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setWindowTitle(tr("Add to Archive"));
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setDirectory(lastPath_);
    if (dialog.exec())
      files = dialog.selectedFiles();
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
    for (auto &file : std::as_const(files))
    {
      if (file.section("/",-1) == item->whatsThis(0)) {
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

  scrollToCurrent_ = false;
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
    if (dir.section("/",-1) == item->whatsThis(0)) {
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

  scrollToCurrent_ = false;
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
  const QList<QTreeWidgetItem*> sel = ui->tree_contents->selectedItems();
  if (sel.isEmpty()) return;
  if (config_.getRemovalPrompt()) {
    if (QMessageBox::question(this,
                              tr("Question"),
                              tr("Do you want to remove the selected item(s)?\n"),
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No)
        == QMessageBox::No) {
      return;
    }
  }
  /* WARNING: 7z isn't self-consistent in file removal: sometimes it needs password,
              sometimes not. Moreover, it may remove files with a wrong password. */
  if (BACKEND->isEncrypted() && BACKEND->getPswrd().isEmpty() && !pswrdDialog())
    return;
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

void mainWin::extractDraggedItems() {
  const QList<QTreeWidgetItem*> sel = ui->tree_contents->selectedItems();
  if (sel.isEmpty()) return;
  if (BACKEND->isEncrypted() && BACKEND->getPswrd().isEmpty()) {
    for (const auto &it : sel) {
      if (subTreeIsEncrypted(it)
          && !BACKEND->isSingleExtracted(it->whatsThis(0))) {
        if (!pswrdDialog()) return;
        break; // if the next password is different, we should wait for the extraction failure
      }
    }
  }
  updateTree_ = false;

  QPointer<QDrag> drag = new QDrag(this);
  QMimeData* mimeData = new QMimeData;
  drag->setMimeData(mimeData);
  connect(BACKEND, &Backend::tempFilesExtracted, drag, [drag](const QStringList &files) {
    QList<QUrl> urlList;
    for (const auto &file : files) {
      urlList << QUrl::fromLocalFile(file);
    }
    drag->mimeData()->setUrls(urlList);
    if (drag && drag->exec(Qt::CopyAction) == Qt::IgnoreAction)
      drag->deleteLater();
  });

  QStringList paths;
  for (const auto &it : sel) {
    paths << it->whatsThis(0);
  }
  BACKEND->extractTempFiles(paths);
}

void mainWin::labelContextMenu(const QPoint& p) {
  QMenu menu(this); // "this" is for Wayland, when the window isn't active
  QAction *action = menu.addAction(config_.getSysIcons()
                                     ? QIcon::fromTheme("edit-copy",
                                                        symbolicIcon::icon(":icons/edit-copy.svg"))
                                     : symbolicIcon::icon(":icons/edit-copy.svg"),
                                   tr("Copy Archive Path"));
  connect(action, &QAction::triggered, [this] {
    QApplication::clipboard()->setText(BACKEND->currentFile());
  });
  if (!isRoot_) {
    menu.addSeparator();
    action = menu.addAction(config_.getSysIcons()
                              ? QIcon::fromTheme("document-open",
                                                 symbolicIcon::icon(":icons/document-open.svg"))
                              : symbolicIcon::icon(":icons/document-open.svg"),
                            tr("Open Containing Folder"));
    connect(action, &QAction::triggered, [this] {
      QDBusMessage methodCall =
      QDBusMessage::createMethodCall("org.freedesktop.FileManager1",
                                     "/org/freedesktop/FileManager1",
                                     "",
                                     "ShowItems");
      methodCall.setAutoStartService (false); // needed for switching to URL opening
      QList<QVariant> args;
      args.append(QStringList() << BACKEND->currentFile());
      args.append("0");
      methodCall.setArguments(args);
      QDBusMessage response = QDBusConnection::sessionBus().call(methodCall, QDBus::Block, 1000);
      if (response.type() == QDBusMessage::ErrorMessage) {
        QString folder = BACKEND->currentFile().section("/", 0, -2);
        if (QStandardPaths::findExecutable("gio").isEmpty()
            || !QProcess::startDetached("gio", QStringList() << "open" << folder)) {
          QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
        }
      }
    });
  }
  menu.exec(ui->frame->mapToGlobal(p));
}

void mainWin::listContextMenu(const QPoint& p) {
  QModelIndex index = ui->tree_contents->indexAt(p);
  if (!index.isValid()) return;
  QTreeWidgetItem *item = ui->tree_contents->getItemFromIndex(index);

  QMenu menu(this); // "this" is for Wayland, when the window isn't active;
  menu.addAction(ui->actionExtractSel);
  if (item && !item->text(1).isEmpty())
    menu.addAction(ui->actionView);
  menu.addSeparator();
  menu.addAction(ui->actionCopy);
  menu.exec(ui->tree_contents->viewport()->mapToGlobal(p));
}

void mainWin::onChangingExpansion(QTreeWidgetItem* /*item*/) {
  adjustColumnSizes();
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
  QPushButton *cancelButton = new QPushButton (config_.getSysIcons()
                                                 ? QIcon::fromTheme("dialog-error",
                                                                    symbolicIcon::icon(":icons/dialog-error.svg"))
                                                 : symbolicIcon::icon(":icons/dialog-error.svg"),
                                               tr("Cancel"));
  QPushButton *okButton = new QPushButton (config_.getSysIcons()
                                             ? QIcon::fromTheme("dialog-ok",
                                                                symbolicIcon::icon(":icons/dialog-ok.svg"))
                                             : symbolicIcon::icon(":icons/dialog-ok.svg"),
                                           tr("OK"));
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

  if (!listEncryptionBox) {
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
  if (BACKEND->isEncrypted() && BACKEND->getPswrd().isEmpty()
      && (!BACKEND->hasEncryptedList()
          || BACKEND->hasList()) // pswrdDialog() has already been called by openEncryptedList()
      && !pswrdDialog()) {
    return;
  }
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
  const QList<QTreeWidgetItem*> sel = ui->tree_contents->selectedItems();
  if (sel.isEmpty()) return;
  if (BACKEND->isEncrypted() && BACKEND->getPswrd().isEmpty()) {
    for (const auto &it : sel) {
      if (subTreeIsEncrypted(it)) {
        if (!pswrdDialog()) return;
        break; // if the next password is different, we should wait for the extraction failure
      }
    }
  }

  QStringList selList;
  const QString singleRoot = BACKEND->singleRoot();
  for (int i = 0; i < sel.length(); i++) {
    if (sel.at(i)->whatsThis(0) == singleRoot) { // total extraction
      selList.clear();
      break;
    }
    selList << sel.at(i)->whatsThis(0);
  }

  QString dir = QFileDialog::getExistingDirectory(this, tr("Extract Into Directory"), lastPath_);
  if (dir.isEmpty()) return;

  /* check overwriting with partial extractions */
  bool overwrite(false);
  if (!selList.isEmpty()) {
    selList.sort();
    for (const auto &file : std::as_const(selList))
    {
        /* the path may contain newlines, which have been escaped and are restored here */
        QString realFile(file);
        realFile.replace(QRegularExpression("(?<!\\\\)\\\\n"), "\n")
                .replace(QRegularExpression("(?<!\\\\)\\\\t"), "\t");
        if (!BACKEND->isGzip() && !BACKEND->is7z())
          realFile.replace("\\\\", "\\"); // WARNING: bsdtar escapes backslashes.
        if (QFile::exists(dir + "/" + realFile.section("/",-1))) {
          QMessageBox::StandardButton btn = QMessageBox::question(this,
                                                                  tr("Question"),
                                                                  tr("Some files will be overwritten.\nDo you want to continue?\n"),
                                                                  QMessageBox::Yes | QMessageBox::No,
                                                                  QMessageBox::No);
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
  if (!BACKEND->startViewFile(it->whatsThis(0))) {
    /* The password was nonempty and wrong. Try again after returning! */
    QTimer::singleShot(0, this, [this]() {
      QTreeWidgetItem *cur = ui->tree_contents->currentItem();
      if (cur && ui->tree_contents->selectedItems().contains(cur))
        viewFile(cur);
    });
  }
}

void mainWin::onEnterPressed(QTreeWidgetItem *it) {
  QModelIndex curIndex = ui->tree_contents->getIndexFromItem(it);
  /* select only the current item */
  if (!ui->tree_contents->selectedItems().contains(it)
      || ui->tree_contents->selectedItems().size() > 1) {
    ui->tree_contents->selectionModel()
      ->setCurrentIndex(curIndex,
                        QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
  }
  ui->tree_contents->scrollTo(curIndex);
  /* expand, collapse or view the current item */
  if (it->text(1).isEmpty()) { // it's a directory item
    if (ui->tree_contents->isExpanded(curIndex))
      ui->tree_contents->collapseItem(it);
    else
      ui->tree_contents->expandItem(it);
  }
  else viewFile(it);
}

static inline QString displaySize(const qint64 size, const QLocale &l) {
  static const QStringList labels = {" B", " K", " M", " G", " T"};
  int i = 0;
  double displaySize = size;
  while (displaySize > static_cast<double>(1024) && i < 4) {
    displaySize /= 1024;
    ++i;
  }
  return (l.toString(std::round(displaySize * 10) / 10) + labels.at(i));
}

void mainWin::updateTree() {
  /* WARNING: Disabling updates can result in an invisible window with some window managers. */
  //setUpdatesEnabled(false);

  QStringList files = BACKEND->hierarchy();
  files.sort();
  bool itemAdded = false;

  /* NOTE: With big archives, QHash is much faster than finding items. */
  /* remove items that aren't in the archive and get the remaining items */
  QHash<QString, QTreeWidgetItem*> allPrevItems = cleanTree(files.size() > 10000 ? QStringList() : files);
  QHash<const QString, QTreeWidgetItem*> dirs; // keep track of directory items

  for (const auto& thisFile : std::as_const(files))
  {
    QTreeWidgetItem *item = allPrevItems.value(thisFile);
    if (item != nullptr) { // already in the tree widget
      if (BACKEND->isDir(thisFile))
        dirs.insert(thisFile, item);
      continue;
    }

    QSize icnSize = ui->tree_contents->iconSize();

    QString mime;
    if (!BACKEND->isDir(thisFile))
      mime = BACKEND->getMimeType(thisFile.section("/", -1));
    auto it = new TreeWidgetItem(); // for a natural sorting
    it->setBackend(BACKEND);
    it->setIconSize(icnSize);

    /* set texts and icons
       (the emblemized icons are set by TreeWidgetItem::data) */
    it->setText(0, thisFile.section("/", -1));
    if (!mime.isEmpty()) {
      it->setText(3, "0"); // to put it after directory items
      if (!BACKEND->isLink(thisFile)) {
        it->setText(1, mime);

        auto s = BACKEND->size(thisFile);
        if (s <= static_cast<double>(0) && files.size() == 1) { // as with "application/x-bzip"
          auto cs = BACKEND->csize(thisFile);
          if (cs > static_cast<double>(0))
            it->setText(2, "≥ " + displaySize(cs, locale()));
          else
            it->setText(2, displaySize(s, locale()));
        }
        else
          it->setText(2, displaySize(s, locale()));

        if (icnSize.width() > 16 && BACKEND->isEncryptedPath(thisFile))
          it->setData(3, Qt::UserRole, "lock"); // to be used in TreeWidgetItem::data() and cleanTree()
        else
          it->setIcon(0, QIcon::fromTheme(mime.replace('/', '-'), symbolicIcon::icon(":icons/unknown.svg")));
      }
      else {
        it->setText(1, tr("Link To: %1").arg(BACKEND->linkTo(thisFile)));
        if (icnSize.width() <= 16) // too small to have an emblem
          it->setIcon(0, QIcon::fromTheme(mime.replace('/', '-'), symbolicIcon::icon(":icons/unknown.svg")));
      }
    }
    else
      it->setIcon(0, QIcon::fromTheme("folder", symbolicIcon::icon(":icons/document-open.svg")));

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
        QStringList sections = thisFile.split("/", Qt::SkipEmptyParts);
        if (!sections.isEmpty()) { // can be empty in rare cases (with "application/x-archive", for example)
          sections.removeLast();
          QTreeWidgetItem *parentItem = nullptr;
          QString theFile;
          for (const auto& thisSection : std::as_const(sections))
          {
            theFile += (theFile.isEmpty() ? QString() : "/") + thisSection;
            QTreeWidgetItem *thisParent = dirs.value(theFile);
            if (!thisParent) {
              QTreeWidgetItem *thisItem = new QTreeWidgetItem();
              thisItem->setText(0, thisSection);
              thisItem->setIcon(0, QIcon::fromTheme("folder", symbolicIcon::icon(":icons/document-open.svg")));
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
          if (parentItem)
            parentItem->addChild(it);
          else
            ui->tree_contents->addTopLevelItem(it);
        }
      }
    }
    else
      ui->tree_contents->addTopLevelItem(it);

    if (BACKEND->isDir(thisFile))
      dirs.insert(thisFile, it);

    itemAdded = true;
  }

  if (itemAdded) {
    adjustColumnSizes(); /* may not be called by eventFilter()
                            because scrollbars may be transient */
    /* sort items by their names but put directory items first */
    QTimer::singleShot(0, this, [this]() {
      ui->tree_contents->sortItems(0, Qt::AscendingOrder);
      ui->tree_contents->sortItems(3, Qt::AscendingOrder);
    });
  }

  if (expandAll_) {
    if (itemAdded) {
      /* WARNING: Contrary to what Qt doc says, QTreeWidget::itemExpanded() is called with expandAll(). */
      disconnect(ui->tree_contents, &QTreeWidget::itemCollapsed, this, &mainWin::onChangingExpansion);
      disconnect(ui->tree_contents, &QTreeWidget::itemExpanded, this, &mainWin::onChangingExpansion);
      if (config_.getExpandTopDirs()) {
        int tc = ui->tree_contents->topLevelItemCount();
        for (int i = 0; i < tc; ++i) {
          QTreeWidgetItem *item = ui->tree_contents->topLevelItem(i);
          if (item->text(1).isEmpty()) {
            ui->tree_contents->expandItem(item);
          }
        }
      }
      else
        ui->tree_contents->expandAll();
      connect(ui->tree_contents, &QTreeWidget::itemExpanded, this, &mainWin::onChangingExpansion);
      connect(ui->tree_contents, &QTreeWidget::itemCollapsed, this, &mainWin::onChangingExpansion);

      QTimer::singleShot(0, this, [this]() {
        if (QAbstractItemModel *model = ui->tree_contents->model()) {
          QModelIndex firstIndx = model->index(0, 0);
          if (firstIndx.isValid())
            ui->tree_contents->selectionModel()->setCurrentIndex(firstIndx, QItemSelectionModel::NoUpdate);
        }
      });
    }
    expandAll_ = false;
  }

  enableActions(true); // should be done immediately (see procFinished)
  QTimer::singleShot(0, this, [this]() {
    if (scrollToCurrent_)
      ui->tree_contents->scrollTo(ui->tree_contents->currentIndex());
    //setUpdatesEnabled(true);
    ui->tree_contents->setEnabled(true);
    ui->tree_contents->update(); // sometimes needed
    ui->tree_contents->setFocus();
    if (QGuiApplication::overrideCursor() != nullptr)
      QGuiApplication::restoreOverrideCursor();
  });
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
  ui->actionView->setEnabled(enable);
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
  ui->label_archive->setText(BACKEND->currentFile().replace('\n', ' ').replace('\t', ' '));
  setWindowTitle(QString ("[*]%1").arg(BACKEND->currentFile().section("/",-1)));

  if (QGuiApplication::overrideCursor() == nullptr)
    QGuiApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
}

void mainWin::procFinished(bool success, const QString& msg) {
  processIsRunning_ = false;

  ui->label->setVisible(true);
  ui->label_archive->setVisible(true);
  ui->label_archive->setText(BACKEND->currentFile().replace('\n', ' ').replace('\t', ' '));
  setWindowTitle(QString ("[*]%1").arg(BACKEND->currentFile().section("/",-1)));

  if (updateTree_) {
    if (success) {
      ui->tree_contents->header()->setVisible(true);
      updateTree();
    }
    else {
      ui->tree_contents->clear();
      ui->tree_contents->header()->setVisible(false);
      ui->tree_contents->setEnabled(true);
      ui->tree_contents->setFocus();
      enableActions(true);
    }
  }
  else {
    ui->tree_contents->setEnabled(true);
    enableActions(true);
  }
  statusProgress_->setRange(0, 0);
  statusProgress_->setValue(0);
  statusProgress_->setVisible(false);
  lastMsg_ = msg;

  QTreeWidgetItem *cur = ui->tree_contents->currentItem();
  bool curSelected = false;
  if (cur && ui->tree_contents->selectedItems().contains(cur))
  {
    curSelected = true;
    textLabel_->setText(cur->whatsThis(0).replace('\n', ' ').replace('\t', ' '));
  }
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

  bool _canUpdate;
  bool _canModify = BACKEND->canModify(&_canUpdate);
  ui->actionUpdate->setVisible(_canUpdate);
  if (updateTree_ && !success) {
    ui->actionAddFile->setEnabled(false);
    ui->actionRemoveFile->setEnabled(false);
    ui->actionExtractAll->setEnabled(false);
    ui->actionExtractSel->setEnabled(false);
    ui->actionView->setEnabled(false);
    ui->actionAddDir->setEnabled(false);
    ui->actionPassword->setEnabled(false);
  }
  else {
    QFileInfo info(BACKEND->currentFile());
    canModify_ = info.isWritable();
    if (!info.exists())
      canModify_ = QFileInfo(BACKEND->currentFile().section("/", 0, -2)).isWritable();
    canUpdate_ = canModify_ && _canUpdate;
    canModify_ = canModify_ && _canModify; // also include the file type limitations
    ui->actionAddFile->setEnabled(canModify_ && (!BACKEND->isGzip() || ui->tree_contents->topLevelItemCount() == 0));
    ui->actionExtractAll->setEnabled(info.exists() && ui->tree_contents->topLevelItemCount() > 0);
    bool hasSelection = info.exists() && !ui->tree_contents->selectedItems().isEmpty();
    ui->actionExtractSel->setEnabled(hasSelection);
    ui->actionRemoveFile->setEnabled(hasSelection && canModify_ && !BACKEND->isGzip());
    ui->actionView->setEnabled(curSelected && !cur->text(1).isEmpty());
    ui->actionAddDir->setEnabled(canModify_ && !BACKEND->isGzip());
    ui->actionPassword->setEnabled(BACKEND->is7z() && canModify_);
  }

  if (!(updateTree_ && success) // otherwise, the cursor will be restored in updateTree()
      && QGuiApplication::overrideCursor() != nullptr)
  {
    QGuiApplication::restoreOverrideCursor();
  }

  updateTree_ = true;
  scrollToCurrent_ = true;
  if (close_)
    QTimer::singleShot(500, this, &QWidget::close);
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
    /* clear contents because nothing is loaded */
    ui->tree_contents->clear();
    ui->tree_contents->header()->setVisible(false);
    /* also, prevent confusion by disabling actions and showing a status message */
    ui->actionAddFile->setEnabled(false);
    ui->actionRemoveFile->setEnabled(false);
    ui->actionExtractAll->setEnabled(false);
    ui->actionAddDir->setEnabled(false);
    ui->actionExtractSel->setEnabled(false);
    ui->actionView->setEnabled(false);
    ui->actionPassword->setEnabled(false);
    textLabel_->setText(tr("Could not read archive"));
    return;
  }
  axFileList_.removeOne(path);
  BACKEND->loadFile(path, true);
}

void mainWin::selectionChanged() {
  bool hasSelection = !ui->tree_contents->selectedItems().isEmpty();
  ui->actionExtractSel->setEnabled(hasSelection);
  ui->actionRemoveFile->setEnabled(hasSelection && canModify_ && !BACKEND->isGzip());
  QTreeWidgetItem *cur = ui->tree_contents->currentItem();
  if (cur && ui->tree_contents->selectedItems().contains(cur))
  {
    ui->actionView->setEnabled(!cur->text(1).isEmpty());
    textLabel_->setText(cur->whatsThis(0).replace('\n', ' ').replace('\t', ' '));
  }
  else
  {
    ui->actionView->setEnabled(false);
    textLabel_->setText(lastMsg_);
  }
}

void mainWin::adjustColumnSizes() {
  ui->tree_contents->header()->setSectionResizeMode(0, QHeaderView::Interactive);
  QTimer::singleShot(0, this, [this]() {
    ui->tree_contents->resizeColumnToContents(2);
  });
  QTimer::singleShot(0, this, [this]() {
    ui->tree_contents->resizeColumnToContents(1);
  });
  QTimer::singleShot(0, this, [this]() {
    ui->tree_contents->setColumnWidth(0, std::max(ui->tree_contents->getSizeHintForColumn(0),
                                                  ui->tree_contents->viewport()->width()
                                                    - ui->tree_contents->columnWidth(1)
                                                    - ui->tree_contents->columnWidth(2)));
  });
}

void mainWin::prefDialog() {
    PrefDialog dlg(this);
    dlg.exec();
}

void mainWin::aboutDialog() {
  class AboutDialog : public QDialog {
  public:
    explicit AboutDialog(QWidget* parent = nullptr, Qt::WindowFlags f = Qt::WindowFlags()) : QDialog(parent, f)
    {
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
  dialog.setMainIcon(QIcon::fromTheme("arqiver", QIcon(":icons/arqiver.svg")));
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
