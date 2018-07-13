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

#ifndef CONFIG_H
#define CONFIG_H

#include <QSettings>
#include <QSize>

namespace Arqiver {

// Prevent redundant writings! (Why does QSettings write to the config file when no setting is changed?)
class Settings : public QSettings
{
    Q_OBJECT
public:
  Settings(const QString &organization, const QString &application = QString(), QObject *parent = nullptr)
          : QSettings(organization, application, parent) {}

  void setValue(const QString &key, const QVariant &v) {
    if (value (key) == v)
      return;
    QSettings::setValue(key, v);
  }
};

class Config {
public:
  Config();
  ~Config();

  void readConfig();
  void writeConfig();

  bool getRemSize() const {
    return remSize_;
  }
  void setRemSize(bool rem) {
    remSize_ = rem;
  }

  bool getIsMaxed() const {
      return isMaxed_;
  }
  void setIsMaxed(bool isMaxed) {
    isMaxed_ = isMaxed;
  }

  QSize getWinSize() const {
    return winSize_;
  }
  void setWinSize(QSize s) {
    winSize_ = s;
  }

  QSize getStartSize() const {
    return startSize_;
  }
  void setStartSize(QSize s) {
    startSize_ = s;
  }

  QString getLastFilter() const {
    return lastFilter_;
  }
  void setLastFilter(const QString& last) {
    lastFilter_ = last;
  }

  int getIconSize() const {
    return iconSize_;
  }
  void setIconSize(int size) {
    iconSize_ = size;
  }

private:
  bool remSize_, isMaxed_;
  QSize winSize_, startSize_;
  int iconSize_;
  QString lastFilter_;
};

}

#endif // CONFIG_H
