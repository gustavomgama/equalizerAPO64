/*
    This file is part of Equalizer APO, a system-wide equalizer.

    Linux-aware VST plugin path resolution. See VSTPluginResolver.h.
*/

#include "VSTPluginResolver.h"

#ifdef _WIN32
#include "RegistryHelper.h"
#else
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#endif
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>

namespace eqapo
{
namespace
{
std::string narrowPath(const std::wstring& w)
{
	return std::string(w.begin(), w.end());
}

std::wstring widenPath(const std::string& s)
{
	return std::wstring(s.begin(), s.end());
}

std::string lowerExt(const std::string& path)
{
	std::string ext = std::filesystem::path(path).extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(),
		[](unsigned char c) { return (char)std::tolower(c); });
	return ext;
}

bool pathExists(const std::string& path, bool& isDir)
{
	struct stat st;
	if (stat(path.c_str(), &st) != 0)
		return false;
	isDir = S_ISDIR(st.st_mode);
	return true;
}
}

bool fileMagicMatches(const std::wstring& path, const char* magic, size_t len)
{
	const std::string n = narrowPath(path);
	FILE* f = fopen(n.c_str(), "rb");
	if (!f)
		return false;
	char buf[8] = {0};
	size_t nread = fread(buf, 1, len > sizeof(buf) ? sizeof(buf) : len, f);
	fclose(f);
	return nread >= len && std::memcmp(buf, magic, len) == 0;
}

std::wstring yabridgeWrapperFor(const std::wstring& windowsPath, const std::string& home)
{
	const std::string n = narrowPath(windowsPath);
	const std::filesystem::path p(n);
	const std::string ext = lowerExt(n);
	const std::string stem = p.stem().string();
	if (stem.empty() || home.empty())
		return L"";
	// yabridge 5 defaults: VST2 wrappers in ~/.vst/yabridge/<name>.so,
	// VST3 wrappers in ~/.vst3/yabridge/<name>.vst3.
	if (ext == ".dll")
		return widenPath(home + "/.vst/yabridge/" + stem + ".so");
	if (ext == ".vst3")
		return widenPath(home + "/.vst3/yabridge/" + stem + ".vst3");
	return L"";
}

ResolvedPlugin resolvePluginPath(const std::wstring& libPath)
{
	ResolvedPlugin out;
#ifdef _WIN32
	// Windows loads .dll/.vst3 natively; keep historical behavior.
	out.path = libPath;
	out.kind = PluginKind::NativeVST2;
	return out;
#else
	const std::string n = narrowPath(libPath);
	const std::string ext = lowerExt(n);
	bool isDir = false;
	if (!pathExists(n, isDir))
	{
		out.kind = PluginKind::Missing;
		return out;
	}

	// A .vst3 bundle directory is a container, not a loadable file.
	if (isDir)
	{
		if (ext == ".vst3")
		{
			out.path = libPath;
			out.kind = PluginKind::NativeVST3;
			return out;
		}
		out.kind = PluginKind::Unsupported;
		return out;
	}

	static const char kMZ[] = {'M', 'Z'};
	static const char kELF[] = {'\x7f', 'E', 'L', 'F'};
	const bool isPE = fileMagicMatches(libPath, kMZ, 2);
	const bool isELF = fileMagicMatches(libPath, kELF, 4);
	if (isPE && !isELF)
	{
		const char* home = getenv("HOME");
		const std::string homeStr = home ? home : "";
		const std::wstring wrapper = yabridgeWrapperFor(libPath, homeStr);
		const bool wrapperExists = !wrapper.empty() &&
			pathExists(narrowPath(wrapper), isDir);
		if (ext == ".dll")
		{
			out.kind = PluginKind::WindowsVST2;
			if (wrapperExists)
			{
				out.path = wrapper;
				out.kind = PluginKind::NativeVST2;
				return out;
			}
		}
		else if (ext == ".vst3")
		{
			out.kind = PluginKind::WindowsVST3;
			if (wrapperExists)
			{
				out.path = wrapper;
				out.kind = PluginKind::NativeVST3;
				return out;
			}
		}
		else
		{
			out.kind = PluginKind::Unsupported;
			return out;
		}

		// Windows binary, no wrapper: tell the user exactly what to run.
		std::filesystem::path dir(n);
		dir = dir.has_parent_path() ? dir.parent_path() : std::filesystem::path(".");
		std::string hint = "Windows plugin '" + n + "' cannot be loaded directly. "
			"Install wine and yabridge (AUR), then run: yabridgectl add \"" +
			dir.string() + "\" && yabridgectl sync";
		if (!wrapper.empty())
			hint += " (wrapper expected at " + narrowPath(wrapper) + ")";
		out.hint = widenPath(hint);
		out.path = L"";
		return out;
	}

	if (isELF)
	{
		out.path = libPath;
		out.kind = (ext == ".vst3") ? PluginKind::NativeVST3 : PluginKind::NativeVST2;
		return out;
	}

	out.kind = PluginKind::Unsupported;
	return out;
#endif
}
}
