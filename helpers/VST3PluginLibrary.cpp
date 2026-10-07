/*
    This file is part of Equalizer APO, a system-wide equalizer.

    Linux-only VST3 module loader. Not part of the Windows build.
*/

#include "stdafx.h"
#include "VST3PluginLibrary.h"

#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <sys/stat.h>

#include "AbstractLibrary.h"
#include "helpers/LogHelper.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"

using namespace std;
namespace fs = std::filesystem;

std::unordered_map<std::wstring, std::weak_ptr<VST3PluginLibrary>> VST3PluginLibrary::instanceMap;

std::shared_ptr<VST3PluginLibrary> VST3PluginLibrary::getInstance(const wstring& libPath)
{
	shared_ptr<VST3PluginLibrary> ptr;
	auto it = instanceMap.find(libPath);
	if (it != instanceMap.end())
		ptr = it->second.lock();
	if (!ptr)
	{
		ptr = shared_ptr<VST3PluginLibrary>(new VST3PluginLibrary(libPath));
		instanceMap[libPath] = ptr;
	}
	return ptr;
}

VST3PluginLibrary::VST3PluginLibrary(const wstring& libPath)
	: libPath(libPath)
{
}

VST3PluginLibrary::~VST3PluginLibrary()
{
	if (pluginFactory != nullptr)
	{
		pluginFactory->release();
		pluginFactory = nullptr;
	}
	if (module != nullptr)
	{
		dlclose(module);
		module = nullptr;
	}
}

std::wstring VST3PluginLibrary::getLibPath() const
{
	return libPath;
}

bool VST3PluginLibrary::isInitialized() const
{
	return ready;
}

std::string VST3PluginLibrary::effectName() const
{
	return effectNameStored;
}

Steinberg::IPluginFactory* VST3PluginLibrary::factory() const
{
	return pluginFactory;
}

// A .vst3 bundle directory maps to Contents/<arch>/<name>.so (preferring the
// Linux x86_64 build); a plain file is used directly.
std::string VST3PluginLibrary::resolveModule(const std::string& path)
{
	std::error_code ec;
	fs::path p(path);
	if (!fs::is_directory(p, ec))
		return path;

	const char* archDirs[] = {"x86_64-linux", "x86_64-win", "i386-linux", "arm64-linux", "arm-linux"};
	for (const char* arch : archDirs)
	{
		fs::path dir = p / "Contents" / arch;
		if (!fs::is_directory(dir, ec))
			continue;
		for (const auto& entry : fs::directory_iterator(dir, ec))
		{
			if (entry.is_regular_file(ec) && entry.path().extension() == ".so")
				return entry.path().string();
		}
	}
	fs::path contents = p / "Contents";
	if (fs::is_directory(contents, ec))
	{
		for (auto it = fs::recursive_directory_iterator(contents, ec);
			it != fs::recursive_directory_iterator(); ++it)
		{
			if (it->is_regular_file(ec) && it->path().extension() == ".so")
				return it->path().string();
		}
	}
	return {};
}

int VST3PluginLibrary::initialize()
{
	if (ready)
		return 0;

	const std::string narrow(libPath.begin(), libPath.end());
	struct stat st;
	if (stat(narrow.c_str(), &st) != 0)
		return AbstractLibrary::FILE_NOT_FOUND;

	const std::string modulePath = resolveModule(narrow);
	if (modulePath.empty())
		return AbstractLibrary::FILE_NOT_FOUND;

	module = dlopen(modulePath.c_str(), RTLD_NOW | RTLD_LOCAL);
	if (module == nullptr)
	{
		TraceF(L"Could not load VST3 module %s: %S",
			libPath.c_str(), dlerror());
		return AbstractLibrary::LOADING_FAILED;
	}

	typedef Steinberg::IPluginFactory* (*GetFactoryFunc)();
	GetFactoryFunc getFactory =
		(GetFactoryFunc)dlsym(module, "GetPluginFactory");
	if (getFactory == nullptr)
	{
		dlclose(module);
		module = nullptr;
		return AbstractLibrary::FUNCTIONS_MISSING;
	}

	pluginFactory = getFactory();
	if (pluginFactory == nullptr)
	{
		dlclose(module);
		module = nullptr;
		return AbstractLibrary::FUNCTIONS_MISSING;
	}

	// Pick the first audio effect class.
	bool found = false;
	const Steinberg::int32 count = pluginFactory->countClasses();
	for (Steinberg::int32 i = 0; i < count && !found; i++)
	{
		Steinberg::PClassInfo info{};
		if (pluginFactory->getClassInfo(i, &info) != Steinberg::kResultOk)
			continue;
		if (std::strcmp(info.category, kVstAudioEffectClass) == 0)
		{
			std::memcpy(effectCID, info.cid, sizeof(effectCID));
			effectNameStored = info.name;
			found = true;
		}
	}
	if (!found)
	{
		pluginFactory->release();
		pluginFactory = nullptr;
		dlclose(module);
		module = nullptr;
		return AbstractLibrary::FUNCTIONS_MISSING;
	}

	ready = true;
	TraceF(L"Loaded VST3 plugin %s (%S)", libPath.c_str(), effectNameStored.c_str());
	return 1;
}

Steinberg::Vst::IComponent* VST3PluginLibrary::createComponent()
{
	if (!ready || pluginFactory == nullptr)
		return nullptr;
	Steinberg::Vst::IComponent* component = nullptr;
	Steinberg::tresult r = pluginFactory->createInstance(effectCID, Steinberg::Vst::IComponent::iid,
		(void**)&component);
	if (r != Steinberg::kResultOk)
		return nullptr;
	return component;
}
