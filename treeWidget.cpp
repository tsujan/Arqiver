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

#include "treeWidget.h"
#include <QUrl>
#include <QScrollBar>
#include <QMimeData>
#include <QIcon>
#include <QApplication>

#define SCROLL_FRAMES_PER_SEC 50
#define SCROLL_DURATION 300 // in ms
static const int scrollAnimFrames = SCROLL_FRAMES_PER_SEC * SCROLL_DURATION / 1000;

namespace Arqiver {

TreeWidget::TreeWidget(QWidget *parent) : QTreeWidget(parent) {
  dragStarted_ = false; // not needed
  enterPressedHere_ = false;
  setDragDropMode(QAbstractItemView::DragOnly);
  setContextMenuPolicy(Qt::CustomContextMenu);
  setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  smoothScrollTimer_ = nullptr;

  /* set the scrolling step to the row height (Qt should have done it, IMO) */
  if (model()) { // we don't change the model
    connect(model(), &QAbstractItemModel::rowsInserted, this, [this] (const QModelIndex &parent, int first, int last) {
      if (!parent.isValid() && first == 0 && last == 0)
        verticalScrollBar()->setSingleStep(rowHeight(model()->index(0,0)));
    });
  }
}
/*************************/
TreeWidget::~TreeWidget() {
  if (smoothScrollTimer_) {
    disconnect(smoothScrollTimer_, &QTimer::timeout, this, &TreeWidget::scrollSmoothly);
    smoothScrollTimer_->stop();
    delete smoothScrollTimer_;
  }
}
/*************************/
// The default implementation doesn't scroll to a deep nonexpanded item
// because it doesn't have time to.
void TreeWidget::scrollTo(const QModelIndex &index, QAbstractItemView::ScrollHint hint) {
  if (!index.isValid()) return;
  if (itemsExpandable()) {
    QModelIndex parent = index.parent();
    while (parent != rootIndex() && parent.isValid() && state() == QAbstractItemView::NoState) {
      if (!isExpanded(parent))
        expand(parent);
      parent = model()->parent(parent);
    }
  }
  QTreeWidget::scrollTo(index, hint);
}
/*************************/
void TreeWidget::mousePressEvent(QMouseEvent *event) {
  QTreeWidget::mousePressEvent(event);
  if (event->button() == Qt::LeftButton && indexAt(event->pos()).isValid())
    dragStartPosition_ = event->pos();
  else
    dragStartPosition_ = QPoint();
  dragStarted_ = false;
}
/*************************/
void TreeWidget::mouseMoveEvent(QMouseEvent *event) {
  if (dragStartPosition_.isNull()) {
    event->ignore();
    return;
  }

  if (!dragStarted_
      && (event->buttons() & Qt::LeftButton)
      && (event->pos() - dragStartPosition_).manhattanLength() >= qMax(22, QApplication::startDragDistance())) {
    dragStarted_ = true;
    if (!selectedItems().isEmpty())
      emit dragStarted();
    event->accept();
  }
}
/*************************/
void TreeWidget::keyReleaseEvent(QKeyEvent *event) {
  if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
    /* NOTE: If Enter is kept pressed, it will be released and then will be
             pressed again, and so on. So, auto-repeating is ignored here. */
    if (enterPressedHere_ && !event->isAutoRepeat() && currentItem() && !currentItem()->isHidden()) {
      emit enterPressed(currentItem());
      event->accept();
      enterPressedHere_ = false;
      return;
    }
    enterPressedHere_ = false;
  }
  QTreeWidget::keyReleaseEvent(event);
}
/*************************/
void TreeWidget::keyPressEvent(QKeyEvent *event) {
  if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
    enterPressedHere_ = true; // to know if it's pressed here and, e.g., not inside a modal dialog
  else if (event->key() != Qt::Key_Up && event->key() != Qt::Key_Down
           && event->key() != Qt::Key_Home && event->key() != Qt::Key_End
           && event->key() != Qt::Key_PageUp && event->key() != Qt::Key_PageDown
           && !(event->modifiers() == Qt::ControlModifier && event->key() == Qt::Key_A)) {
    /* ignore typing */
    event->accept();
    return;
  }
  QTreeWidget::keyPressEvent(event);
}
/*************************/
void TreeWidget::wheelEvent(QWheelEvent *event) {
  /* smooth scrolling */
  if (event->spontaneous()
      && event->source() == Qt::MouseEventNotSynthesized
      && event->angleDelta().x() == 0) { // vertical scrolling
    if (QScrollBar* vbar = verticalScrollBar()) {
      /* keep track of the wheel event for smooth scrolling */
      int delta = event->angleDelta().y();
      if (QApplication::keyboardModifiers() & Qt::ShiftModifier) {
        delta /= QApplication::wheelScrollLines(); // row-by-row scrolling
      }
      if ((delta > 0 && vbar->value() == vbar->minimum())
          || (delta < 0 && vbar->value() == vbar->maximum())) {
        return; // the scrollbar can't move
      }

      if (smoothScrollTimer_ == nullptr) {
          smoothScrollTimer_ = new QTimer();
          connect(smoothScrollTimer_, &QTimer::timeout, this, &TreeWidget::scrollSmoothly);
      }

      /* set the data for smooth scrolling */
      scrollData data;
      data.delta = delta;
      data.leftFrames = scrollAnimFrames;
      queuedScrollSteps_.append(data);
      smoothScrollTimer_->start(1000 / SCROLL_FRAMES_PER_SEC);
      return;
    }
  }

  QTreeWidget::wheelEvent (event);
}
/*************************/
void TreeWidget::scrollSmoothly() {
  if (!verticalScrollBar()) // impossible
    return;

  int totalDelta = 0;
  QList<scrollData>::iterator it = queuedScrollSteps_.begin();
  while (it != queuedScrollSteps_.end()) {
    int delta = qRound(static_cast<qreal>(it->delta) / static_cast<qreal>(scrollAnimFrames));
    int remainingDelta = it->delta - (scrollAnimFrames - it->leftFrames) * delta;
    if (qAbs(delta) >= qAbs(remainingDelta)) {
      /* this is the last frame or, due to rounding, there can be no more frame */
      totalDelta += remainingDelta;
      it = queuedScrollSteps_.erase(it);
    }
    else {
      totalDelta += delta;
      -- it->leftFrames;
      ++it;
    }
  }
  if (totalDelta != 0) {
#if (QT_VERSION >= QT_VERSION_CHECK(5,12,0))
    QWheelEvent e(QPointF(),
                  QPointF(),
                  QPoint(),
                  QPoint(0, totalDelta),
                  Qt::NoButton,
                  Qt::NoModifier,
                  Qt::NoScrollPhase,
                  false);
#else
    QWheelEvent e(QPointF(),
                  QPointF(),
                  totalDelta,
                  Qt::NoButton,
                  Qt::NoModifier,
                  Qt::Vertical);
#endif
    QApplication::sendEvent(verticalScrollBar(), &e);
  }

  if (queuedScrollSteps_.empty()) {
      smoothScrollTimer_->stop();
  }
}

}
