/*
    This file is part of Equalizer APO, a system-wide equalizer.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#pragma once

#include <QByteArray>
#include <QDir>
#include <QSettings>
#include <QString>

// On Windows the editor settings live in the registry (EDITOR_REGPATH /
// EDITOR_PER_FILE_REGPATH from MainWindow.h). On Linux they are stored as INI
// files under $XDG_CONFIG_HOME/equalizerapo.
#ifndef _WIN32
inline QString editorSettingsFilePath(bool fileSpecific)
{
	const QByteArray xdg = qgetenv("XDG_CONFIG_HOME");
	const QString base = xdg.isEmpty()
		? QDir::homePath() + QStringLiteral("/.config")
		: QString::fromLocal8Bit(xdg);
	const QString dir = base + QStringLiteral("/equalizerapo");
	QDir().mkpath(dir);
	return dir + (fileSpecific ? QStringLiteral("/Editor-file-specific.conf")
	                           : QStringLiteral("/Editor.conf"));
}
#endif

#ifdef _WIN32
#define EQAPO_EDITOR_SETTINGS(perFile) \
	QSettings settings(QString::fromWCharArray((perFile) ? EDITOR_PER_FILE_REGPATH : EDITOR_REGPATH), QSettings::NativeFormat)
#else
#define EQAPO_EDITOR_SETTINGS(perFile) \
	QSettings settings(editorSettingsFilePath(perFile), QSettings::IniFormat)
#endif
