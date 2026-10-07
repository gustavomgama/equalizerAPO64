/*
    This file is part of Equalizer APO, a system-wide equalizer.

    Linux-aware VST plugin path resolution: detect Windows PE vs native ELF
    and VST2 vs VST3, and map Windows plugins to their yabridge (Wine) wrapper
    instead of trying to dlopen them. Pure functions, no loading.
*/

#pragma once

#include <string>

namespace eqapo
{
enum class PluginKind
{
	Missing,     // path does not exist
	NativeVST2,  // native .so speaking VST2 (incl. yabridge VST2 wrappers)
	NativeVST3,  // native VST3 bundle or .so (needs the VST3 host)
	WindowsVST2, // Windows PE .dll (needs a yabridge wrapper)
	WindowsVST3, // Windows PE .vst3 (needs a yabridge wrapper)
	Unsupported, // exists but is neither
};

struct ResolvedPlugin
{
	std::wstring path; // loadable path, or L"" when it cannot be loaded
	PluginKind kind = PluginKind::Missing;
	std::wstring hint; // actionable message when path is empty
};

// Map a `VSTPlugin: Library` path to something loadable. Never loads anything.
ResolvedPlugin resolvePluginPath(const std::wstring& libPath);

// The native wrapper yabridge generates for a Windows plugin. `home` is
// injected (normally $HOME) so this stays testable without touching env.
std::wstring yabridgeWrapperFor(const std::wstring& windowsPath, const std::string& home);

// True when the file's first bytes equal magic.
bool fileMagicMatches(const std::wstring& path, const char* magic, size_t len);
}
