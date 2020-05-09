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

#ifndef LABEL_H
#define LABEL_H

#include <QLabel>
#include <QTimer>
#include <QPainter>
#include <QStyleOption>

namespace Arqiver {

class Label : public QLabel {
  Q_OBJECT

public:
  explicit Label(QWidget *parent = 0, Qt::WindowFlags f = Qt::WindowFlags()):
  QLabel(parent, f),
  lastWidth_(0) {
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    /* set a min width to prevent the window from widening with long texts */
    setMinimumWidth(fontMetrics().averageCharWidth() * 10);
}

protected:
  /* A simplified version of QLabel::paintEvent()
     without pixmap or shortcut but with eliding. */
  void paintEvent(QPaintEvent */*event*/) {
  QRect cr = contentsRect().adjusted(margin(), margin(), -margin(), -margin());
  QString txt = text();
  /* if the text is changed or its rect is resized (due to window resizing),
     find whether it needs to be elided... */
  if (txt != lastText_ || cr.width() != lastWidth_) {
    lastText_ = txt;
    lastWidth_ = cr.width();
    elidedText_ = fontMetrics().elidedText(txt, Qt::ElideMiddle, cr.width());
  }
  /* ... then, draw the (elided) text */
  QPainter painter(this);
  QStyleOption opt;
  opt.initFrom(this);
  style()->drawItemText(&painter, cr, alignment(), opt.palette, isEnabled(), elidedText_, foregroundRole());
}

private:
    QString elidedText_;
    QString lastText_;
    int lastWidth_;
};

}

#endif // LABEL_H
