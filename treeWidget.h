/*
 * Copyright (C) Pedram Pourang (aka Tsu Jan) 2018-2020 <tsujan2000@gmail.com>
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

#ifndef TREEWIDGET_H
#define TREEWIDGET_H

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QTimer>

namespace Arqiver {

class TreeWidget : public QTreeWidget {
  Q_OBJECT

public:
  TreeWidget(QWidget *parent = nullptr);
  ~TreeWidget() override;

  QTreeWidgetItem* getItemFromIndex(const QModelIndex &index) const {
    return itemFromIndex(index);
  }

  QModelIndex getIndexFromItem(const QTreeWidgetItem *item) const {
    return indexFromItem(item);
  }

  void scrollTo(const QModelIndex &index, QAbstractItemView::ScrollHint hint = QAbstractItemView::EnsureVisible);

signals:
  void dragStarted(QTreeWidgetItem *it);
  void enterPressed(QTreeWidgetItem *it);

protected:
  virtual void mousePressEvent(QMouseEvent *event);
  virtual void mouseMoveEvent(QMouseEvent *event);
  virtual void keyReleaseEvent(QKeyEvent *event);
  virtual void keyPressEvent(QKeyEvent *event);
  virtual void wheelEvent(QWheelEvent *event);

private slots:
  void scrollSmoothly();

private:
  QPoint dragStartPosition_;
  bool dragStarted_;
  bool enterPressedHere_;
  /* smooth scrolling */
  struct scrollData {
    int delta;
    int leftFrames;
  };
  QList<scrollData> queuedScrollSteps_;
  QTimer *smoothScrollTimer_;
};

}

#endif // TREEWIDGET_H
