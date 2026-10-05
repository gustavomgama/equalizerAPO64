/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2017  Jonas Thedering

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
#ifdef _WIN32
#include "RegistryHelper.h"
#else
#include <dlfcn.h>
#include <cstdlib>
#endif
#include "LogHelper.h"
#include "VSTPluginLibrary.h"

using namespace std;

std::unordered_map<std::wstring, std::weak_ptr<VSTPluginLibrary>> VSTPluginLibrary::instanceMap;
std::wstring VSTPluginLibrary::defaultPluginPath;

std::shared_ptr<VSTPluginLibrary> VSTPluginLibrary::getInstance(const wstring& libPath)
{
	shared_ptr<VSTPluginLibrary> ptr;

	auto it = instanceMap.find(libPath);
	if (it != instanceMap.end())
	{
		weak_ptr<VSTPluginLibrary> instance = it->second;
		ptr = instance.lock();
	}

	if (ptr == NULL)
	{
		ptr = shared_ptr<VSTPluginLibrary>(new VSTPluginLibrary(libPath));
		instanceMap[libPath] = ptr;
	}

	return ptr;
}

wstring VSTPluginLibrary::getDefaultPluginPath()
{
	if (defaultPluginPath == L"")
	{
#ifdef _WIN32
		wstring installPath = RegistryHelper::readValue(APP_REGPATH, L"InstallPath");
		defaultPluginPath = installPath + L"\\VSTPlugins";
#else
		const char* home = getenv("HOME");
		std::string h = home ? home : "/tmp";
		defaultPluginPath.assign(h.begin(), h.end());
		defaultPluginPath += L"/.vst";
#endif
	}

	return defaultPluginPath;
}

std::wstring VSTPluginLibrary::getLibPath()
{
	return libPath;
}

bool VSTPluginLibrary::loadFunctions()
{
#ifdef _WIN32
	VSTPluginMain = (vstPluginMain)GetProcAddress(module, "VSTPluginMain");
#else
	VSTPluginMain = (vstPluginMain)dlsym(module, "VSTPluginMain");
	if (VSTPluginMain == NULL)
		VSTPluginMain = (vstPluginMain)dlsym(module, "main");
#endif

	return VSTPluginMain != NULL;
}

VSTPluginLibrary::VSTPluginLibrary(const wstring& libPath)
	: libPath(libPath)
{
}
