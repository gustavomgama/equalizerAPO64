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

#include "stdafx.h"
#include <cstdlib>
#include <sys/stat.h>
#include "ConfigPathHelper.h"

using namespace std;

static wstring utf8ToWide(const string& s)
{
	return wstring(s.begin(), s.end());
}

static string homeDir()
{
	const char* h = getenv("HOME");
	return h ? h : "/tmp";
}

wstring ConfigPathHelper::getConfigDir()
{
	const char* xdg = getenv("XDG_CONFIG_HOME");
	string base = (xdg && *xdg) ? string(xdg) : homeDir() + "/.config";
	return utf8ToWide(base + "/equalizerapo");
}

wstring ConfigPathHelper::getConfigFile()
{
	return getConfigDir() + L"/config.txt";
}

wstring ConfigPathHelper::getStateDir()
{
	const char* xdg = getenv("XDG_STATE_HOME");
	string base = (xdg && *xdg) ? string(xdg) : homeDir() + "/.local/state";
	string dir = base + "/equalizerapo";
	mkdir(dir.c_str(), 0755);
	return utf8ToWide(dir);
}
