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

#include "pref.h"
#include "ui_pref.h"
#include "mainWin.h"

#include <QLocale>
#include <QWindow>
#include <QScreen>

namespace Arqiver {

PrefDialog::PrefDialog(QWidget *parent) : QDialog(parent), ui(new Ui::PrefDialog) {
  ui->setupUi(this);
  parent_ = parent;
  bool remSize;
  QSize startSize;
  if (mainWin *win = qobject_cast<mainWin*>(parent_)) {
    Config config = win->getConfig();
    initialIconSize_ = config.getIconSize();
    remSize = config.getRemSize();
    startSize = config.getStartSize();
  }
  else {
    initialIconSize_ = 24;
    remSize = false;
    startSize = QSize(600, 500);
  }

  ui->promptLabel->setStyleSheet("QLabel {background-color: #7d0000; color: white; border-radius: 3px; margin: 2px; padding: 5px;}");
  ui->promptLabel->hide();

  QString PX = " " + tr("px");

  ui->iconSizeCombo->clear();
  QLocale l = ui->iconSizeCombo->locale();
  l.setNumberOptions(l.numberOptions() | QLocale::OmitGroupSeparator);
  ui->iconSizeCombo->insertItems(0, QStringList() << l.toString(22) + PX
                                                  << l.toString(24) + PX
                                                  << l.toString(32) + PX
                                                  << l.toString(48) + PX
                                                  << l.toString(64) + PX);
  int index = 1;
  switch (initialIconSize_) {
    case 22 :
      index = 0;
      break;
    case 24 :
      index = 1;
      break;
    case 32 :
      index = 2;
      break;
    case 48 :
      index = 3;
      break;
    case 64 :
      index = 4;
      break;
    default :
      index = 1;
      break;
  }
  ui->iconSizeCombo->setCurrentIndex(index);
  connect(ui->iconSizeCombo, QOverload<int>::of(&QComboBox::activated), this, &PrefDialog::prefIconSize);

  ui->winSizeBox->setChecked(remSize);
  ui->startSizeLabel->setEnabled(!remSize);
  ui->spinX->setEnabled(!remSize);
  ui->spinY->setEnabled(!remSize);
  ui->label->setEnabled(!remSize);

  ui->spinX->setSuffix(PX);
  ui->spinY->setSuffix(PX);
  QSize ag;
  if (parent != nullptr) {
    if (QWindow *win = parent->windowHandle()) {
      if (QScreen *sc = win->screen())
        ag = sc->availableVirtualGeometry().size();
    }
  }
  ui->spinX->setMaximum(ag.width());
  ui->spinY->setMaximum(ag.height());
  ui->spinX->setValue(startSize.width());
  ui->spinY->setValue(startSize.height());

  connect(ui->winSizeBox, &QCheckBox::stateChanged, [this] (int state) {
    bool checked(state == Qt::Checked);
    ui->startSizeLabel->setEnabled(!checked);
    ui->spinX->setEnabled(!checked);
    ui->spinY->setEnabled(!checked);
    ui->label->setEnabled(!checked);
    if (mainWin *win = qobject_cast<mainWin*>(parent_)) {
      Config& config = win->getConfig();
      config.setRemSize(checked);
    }
  });

  connect(ui->spinX, QOverload<int>::of(&QSpinBox::valueChanged), this, &PrefDialog::prefStartSize);
  connect(ui->spinY, QOverload<int>::of(&QSpinBox::valueChanged), this, &PrefDialog::prefStartSize);

  ui->winSizeBox->setFocus();
  setTabOrder(ui->winSizeBox, ui->spinX);
  setTabOrder(ui->spinX, ui->spinY);
  setTabOrder(ui->spinY, ui->iconSizeCombo);
  resize(sizeHint());
}

PrefDialog::~PrefDialog() {
  delete ui; ui = nullptr;;
}

void PrefDialog::closeEvent(QCloseEvent *event) {
  event->accept();
}

void PrefDialog::showPrompt(const QString& str) {
  if (!str.isEmpty()) {
    ui->promptLabel->setText("<b>" + str + "</b>");
    ui->promptLabel->show();
  }
  else {
    ui->promptLabel->clear();
    ui->promptLabel->hide();
  }
}

void PrefDialog::prefStartSize(int value) {
  if (mainWin *win = qobject_cast<mainWin*>(parent_)) {
    Config& config = win->getConfig();
    QSize startSize = config.getStartSize();
    if (QObject::sender() == ui->spinX)
      startSize.setWidth (value);
    else if (QObject::sender() == ui->spinY)
      startSize.setHeight (value);
    config.setStartSize (startSize);
  }
}

void PrefDialog::prefIconSize(int index) {
  if (mainWin *win = qobject_cast<mainWin*>(parent_)) {
    Config& config = win->getConfig();
    switch (index) {
      case 0 :
        config.setIconSize(22);
        break;
      case 1 :
        config.setIconSize(24);
        break;
      case 2 :
        config.setIconSize(32);
        break;
      case 3 :
        config.setIconSize(48);
        break;
      case 4 :
        config.setIconSize(64);
        break;
      default :
        config.setIconSize(24);
        break;
    }
    if (config.getIconSize() != initialIconSize_)
      showPrompt(tr("Application restart is needed for changes to take effect."));
    else
      showPrompt();
  }
}

}
