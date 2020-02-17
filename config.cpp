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

#include "config.h"

namespace Arqiver {

Config::Config() :
  remSize_(true),
  isMaxed_(false),
  removalPrompt_(true),
  winSize_(QSize(700, 500)),
  startSize_(QSize(700, 500)),
  iconSize_(24) {
}

Config::~Config() {}

void Config::readConfig() {
  Settings settings("arqiver", "arq");

  settings.beginGroup("window");

  if (settings.value("size") == "none")
    remSize_ = false; // true by default
  else {
    winSize_ = settings.value("size", QSize(700, 500)).toSize();
    if (!winSize_.isValid() || winSize_.isNull())
      winSize_ = QSize(700, 500);
    isMaxed_ = settings.value("max", false).toBool();
  }
  startSize_ = settings.value("startSize", QSize(700, 500)).toSize();
  if (!startSize_.isValid() || startSize_.isNull())
    startSize_ = QSize(700, 500);

  iconSize_ = qMin(qMax(settings.value("iconSize", 24).toInt(), 22), 64);

  removalPrompt_ = settings.value("removalPrompt", true).toBool();

  settings.endGroup();

  settings.beginGroup("archive");
  lastFilter_ = settings.value("lastFilter").toString();
  settings.endGroup();
}
/*************************/
void Config::writeConfig() {
  Settings settings("arqiver", "arq");
  if (!settings.isWritable()) return;

  settings.beginGroup("window");

  if (remSize_) {
    settings.setValue("size", winSize_);
    settings.setValue("max", isMaxed_);
  }
  else {
    settings.setValue("size", "none");
    settings.remove("max");
  }

  settings.setValue("startSize", startSize_);

  settings.setValue("iconSize", iconSize_);

  settings.setValue("removalPrompt", removalPrompt_);

  settings.endGroup();

  settings.beginGroup("archive");
  settings.setValue("lastFilter", lastFilter_);
  settings.endGroup();
}

}
