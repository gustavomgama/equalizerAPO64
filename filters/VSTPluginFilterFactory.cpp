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
#include <Shlwapi.h>
#else
#include <filesystem>
#endif
#include "helpers/StringHelper.h"
#include "helpers/VSTPluginLibrary.h"
#include "helpers/VSTPluginResolver.h"
#include "helpers/LogHelper.h"
#include "VSTPluginFilter.h"
#include "VSTPluginFilterFactory.h"
#ifndef _WIN32
#include "helpers/VST3PluginLibrary.h"
#include "VST3PluginFilter.h"
#endif

using namespace std;

vector<IFilter*> VSTPluginFilterFactory::createFilter(const wstring& configPath, wstring& command, wstring& parameters)
{
	IFilter* filter = NULL;

	if (command == L"VSTPlugin")
	{
		shared_ptr<VSTPluginLibrary> library;
		wstring chunkData;
		unordered_map<wstring, float> paramMap;
		vector<wstring> parts = StringHelper::splitQuoted(parameters, ' ');
		for (unsigned i = 0; i + 1 < parts.size(); i += 2)
		{
			wstring key = parts[i];
			wstring value = parts[i + 1];

			if (key == L"Library")
			{
				wstring libPath;
#ifdef _WIN32
				if (PathIsRelativeW(value.c_str()))
				{
					wchar_t filePath[MAX_PATH];
					wstring pluginPath = VSTPluginLibrary::getDefaultPluginPath();
					pluginPath._Copy_s(filePath, sizeof(filePath) / sizeof(wchar_t), MAX_PATH);
					if (pluginPath.size() < MAX_PATH)
						filePath[pluginPath.size()] = L'\0';
					else
						filePath[MAX_PATH - 1] = L'\0';
					PathAppendW(filePath, value.c_str());
					libPath = filePath;
				}
				else
					libPath = value;
#else
				{
					std::filesystem::path p(value);
					if (p.is_relative())
						p = std::filesystem::path(VSTPluginLibrary::getDefaultPluginPath()) / p;
					libPath = p.wstring();
				}
#endif

				// Resolve Windows plugins to their native (yabridge) wrapper and
				// detect VST3 before touching the loader. (VST3 hosting is Phase 2.)
				eqapo::ResolvedPlugin resolved = eqapo::resolvePluginPath(libPath);
				if (resolved.path.empty())
				{
					if (configPath != L"")
					{
						if (!resolved.hint.empty())
							LogF(L"%s", resolved.hint.c_str());
						else if (resolved.kind == eqapo::PluginKind::Missing)
							LogF(L"File %s not found", libPath.c_str());
						else
							LogF(L"Library %s could not be loaded", libPath.c_str());
					}
				}
				else if (resolved.kind == eqapo::PluginKind::NativeVST3)
				{
#ifndef _WIN32
					std::shared_ptr<VST3PluginLibrary> library3 =
						VST3PluginLibrary::getInstance(resolved.path);
					bool create = true;
					if (configPath != L"")
					{
						create = false;
						TraceF(L"Adding VST3 plugin %s", library3->getLibPath().c_str());
						int res = library3->initialize();
						if (res < 0)
						{
							if (res == AbstractLibrary::FILE_NOT_FOUND)
								LogF(L"File %s not found", library3->getLibPath().c_str());
							else if (res == AbstractLibrary::LOADING_FAILED)
								LogF(L"Library %s could not be loaded", library3->getLibPath().c_str());
							else
								LogF(L"Library %s does not contain a VST3 effect", library3->getLibPath().c_str());
						}
						else
						{
							create = true;
						}
					}

					if (create)
						filter = new VST3PluginFilter(library3, chunkData, paramMap);
#else
					if (configPath != L"")
						LogF(L"VST3 plugin %s detected (native VST3 support is being added)", resolved.path.c_str());
#endif
				}
				else
				{
					if (resolved.path != libPath)
						TraceF(L"Mapped plugin %s to %s", libPath.c_str(), resolved.path.c_str());
					library = VSTPluginLibrary::getInstance(resolved.path);
				}
			}
			else if (key == L"ChunkData")
			{
				chunkData = value;
			}
			else
			{
				if (!isdigit(value.c_str()[0]))
				{
					int x = i + 2;
					if (x < (int)parts.size())
					{
						float f = wcstof(parts[x].c_str(), NULL);
						paramMap[value.c_str()] = f;
					}
				}
				else
				{
					float f = wcstof(value.c_str(), NULL);
					paramMap[key] = f;
				}
			}
		}

		if (library != NULL)
		{
			bool create = true;
			if (configPath != L"")
			{
				create = false;
				TraceF(L"Adding VST plugin %s", library->getLibPath().c_str());
				int res = library->initialize();
				if (res < 0)
				{
#ifdef _WIN64
					int bitDepth = 64;
#else
					int bitDepth = 32;
#endif
					if (res == AbstractLibrary::FILE_NOT_FOUND)
						LogF(L"File %s not found", library->getLibPath());
					else if (res == AbstractLibrary::LOADING_FAILED)
						LogF(L"Library %s could not be loaded", library->getLibPath());
					else if (res == AbstractLibrary::FUNCTIONS_MISSING)
						LogF(L"Library %s does not contain needed functions", library->getLibPath());
					else if (res == AbstractLibrary::WRONG_ARCHITECTURE)
						LogF(L"Library %s has wrong architecture, must be %d-bit", library->getLibPath(), bitDepth);
				}
				else
				{
					create = true;
				}
			}

			if (create)
			{
				void* mem = MemoryHelper::alloc(sizeof(VSTPluginFilter));
				filter = new(mem) VSTPluginFilter(library, chunkData, paramMap);
			}
		}
	}

	if (filter == NULL)
		return vector<IFilter*>(0);
	return vector<IFilter*>(1, filter);
}
