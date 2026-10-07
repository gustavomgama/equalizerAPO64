/*
    This file is part of Equalizer APO, a system-wide equalizer.

    Linux-only VST3 module loader (bundle/.so, GetPluginFactory). Not part of
    the Windows build.
*/

#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstcomponent.h"

class VST3PluginLibrary
{
public:
	static std::shared_ptr<VST3PluginLibrary> getInstance(const std::wstring& libPath);

	std::wstring getLibPath() const;

	// Resolve bundle to module, dlopen, find GetPluginFactory and the first
	// audio effect class. 1 on success, else AbstractLibrary-style codes.
	int initialize();
	bool isInitialized() const;

	// Create the effect component. Caller owns (must release).
	Steinberg::Vst::IComponent* createComponent();
	Steinberg::IPluginFactory* factory() const;
	std::string effectName() const;

	~VST3PluginLibrary();

private:
	explicit VST3PluginLibrary(const std::wstring& libPath);
	// A .vst3 bundle directory maps to Contents/<arch>/<name>.so; a file is
	// used directly. Returns "" when no module is found.
	static std::string resolveModule(const std::string& path);

	std::wstring libPath;
	void* module = nullptr;
	Steinberg::IPluginFactory* pluginFactory = nullptr;
	Steinberg::TUID effectCID{};
	std::string effectNameStored;
	bool ready = false;

	static std::unordered_map<std::wstring, std::weak_ptr<VST3PluginLibrary>> instanceMap;
};
