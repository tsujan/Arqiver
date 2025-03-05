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

#include <algorithm>
#include <cmath>

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
  if (event->button() == Qt::LeftButton && indexAt(event->position().toPoint()).isValid())
    dragStartPosition_ = event->position().toPoint();
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
      && (event->position().toPoint() - dragStartPosition_).manhattanLength() >= std::max(22, QApplication::startDragDistance())) {
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
      && event->source() == Qt::MouseEventNotSynthesized) {
    QPoint deltaPoint = event->angleDelta();
    bool horizontal(std::abs(deltaPoint.x()) > std::abs(deltaPoint.y()));
    QScrollBar *sbar = horizontal ? horizontalScrollBar()
                                  : verticalScrollBar();
    if (sbar && sbar->isVisible()) {
      /* keep track of the wheel event for smooth scrolling */
      int delta = horizontal ? deltaPoint.x() : deltaPoint.y();
      if ((delta > 0 && sbar->value() == sbar->minimum())
          || (delta < 0 && sbar->value() == sbar->maximum())) {
        return; // the scrollbar can't move
      }

      if (QApplication::wheelScrollLines() > 1) {
        if (horizontal
            || (event->modifiers() & Qt::ShiftModifier)
            || std::abs(delta) < 120) { // touchpad
          if (std::abs(delta) >= scrollAnimFrames * QApplication::wheelScrollLines())
            delta /= QApplication::wheelScrollLines(); // scrolling with minimum speed
        }
        else if (QApplication::wheelScrollLines() > 2
                 && iconSize().height() >= 48
                 && std::abs(delta * 2) >= scrollAnimFrames * QApplication::wheelScrollLines()) {
          /* 2 rows per wheel turn with large icons */
          delta = delta * 2 / QApplication::wheelScrollLines();
        }
      }

      /* wait until the angle delta reaches an acceptable value */
      static int _delta = 0;
      _delta += delta;
      if (abs(_delta) < scrollAnimFrames)
        return;

      if (smoothScrollTimer_ == nullptr) {
        smoothScrollTimer_ = new QTimer();
        connect(smoothScrollTimer_, &QTimer::timeout, this, &TreeWidget::scrollSmoothly);
      }

      /* set the data for smooth scrolling */
      scrollData data;
      data.delta = _delta;
      data.leftFrames = scrollAnimFrames;
      data.vertical = !horizontal;
      queuedScrollSteps_.append(data);
      if (!smoothScrollTimer_->isActive())
        smoothScrollTimer_->start(1000 / SCROLL_FRAMES_PER_SEC);
      _delta = 0;
      return;
    }
  }

  QTreeWidget::wheelEvent (event);
}
/*************************/
void TreeWidget::scrollSmoothly() {
  int totalDeltaH = 0, totalDeltaV = 0;
  QList<scrollData>::iterator it = queuedScrollSteps_.begin();
  while (it != queuedScrollSteps_.end()) {
    int delta = std::round(static_cast<double>(it->delta) / scrollAnimFrames);
    int remainingDelta = it->delta - (scrollAnimFrames - it->leftFrames) * delta;
    if ((delta >= 0 && remainingDelta < 0) || (delta < 0 && remainingDelta >= 0))
      remainingDelta = 0;
    if (std::abs(delta) >= std::abs(remainingDelta)) {
      /* this is the last frame or, due to rounding, there can be no more frame */
      if (it->vertical)
        totalDeltaV += remainingDelta;
      else
        totalDeltaH += remainingDelta;
      it = queuedScrollSteps_.erase(it);
    }
    else {
      if (it->vertical)
        totalDeltaV += delta;
      else
        totalDeltaH += delta;
      -- it->leftFrames;
      ++it;
    }
  }

  if (totalDeltaH != 0) {
    QScrollBar *hbar = horizontalScrollBar();
    if (hbar && hbar->isVisible()) {
      QWheelEvent eventH(QPointF(),
                         QPointF(),
                         QPoint(),
                         QPoint(0, totalDeltaH),
                         Qt::NoButton,
                         Qt::NoModifier,
                         Qt::NoScrollPhase,
                         false);
      QApplication::sendEvent(hbar, &eventH);
    }
  }
  if (totalDeltaV != 0) {
    QScrollBar *vbar = verticalScrollBar();
    if (vbar && vbar->isVisible()) {
      QWheelEvent eventV(QPointF(),
                         QPointF(),
                         QPoint(),
                         QPoint(0, totalDeltaV),
                         Qt::NoButton,
                         Qt::NoModifier,
                         Qt::NoScrollPhase,
                         false);
      QApplication::sendEvent(vbar, &eventV);
    }
  }

  if (queuedScrollSteps_.empty())
    smoothScrollTimer_->stop();
}

}
