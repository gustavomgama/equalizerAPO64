/*
    This file is part of Equalizer APO, a system-wide equalizer.

    Unit tests for VST plugin path resolution: Windows PE vs native ELF and
    yabridge wrapper mapping. Uses a scratch HOME so no real system is touched.
*/

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "helpers/VSTPluginResolver.h"

namespace
{
namespace fs = std::filesystem;

void writeBytes(const fs::path& p, const std::string& bytes)
{
	std::ofstream f(p, std::ios::binary);
	f.write(bytes.data(), (std::streamsize)bytes.size());
}

std::wstring ws(const fs::path& p)
{
	return p.wstring();
}
}

int main()
{
	const fs::path tmp = fs::temp_directory_path() / "eqapo-vst-resolve-test";
	fs::remove_all(tmp);
	fs::create_directories(tmp);
	setenv("HOME", tmp.c_str(), 1);

	// Wrapper layouts: VST2 .so and a VST3 bundle directory.
	fs::create_directories(tmp / ".vst" / "yabridge");
	writeBytes(tmp / ".vst" / "yabridge" / "Win.so", std::string("\x7f""ELF....", 8));
	fs::create_directories(tmp / ".vst3" / "yabridge" / "Plug.vst3");

	// Windows VST2 .dll with a wrapper present -> native wrapper.
	fs::path windll = tmp / "Win.dll";
	writeBytes(windll, "MZ......");
	eqapo::ResolvedPlugin r = eqapo::resolvePluginPath(ws(windll));
	assert(r.kind == eqapo::PluginKind::NativeVST2);
	assert(r.path == ws(tmp / ".vst" / "yabridge" / "Win.so"));

	// Windows VST2 .dll with no wrapper -> actionable hint, nothing to load.
	fs::path nowin = tmp / "NoWrap.dll";
	writeBytes(nowin, "MZ");
	r = eqapo::resolvePluginPath(ws(nowin));
	assert(r.kind == eqapo::PluginKind::WindowsVST2);
	assert(r.path.empty());
	assert(r.hint.find(L"yabridgectl") != std::wstring::npos);

	// Windows VST3 file with a wrapper bundle -> native bundle.
	fs::path winvst = tmp / "Plug.vst3";
	writeBytes(winvst, "MZ..");
	r = eqapo::resolvePluginPath(ws(winvst));
	assert(r.kind == eqapo::PluginKind::NativeVST3);
	assert(r.path == ws(tmp / ".vst3" / "yabridge" / "Plug.vst3"));

	// Windows VST3 file with no wrapper -> hint.
	fs::path nowrapvst = tmp / "NoWrap.vst3";
	writeBytes(nowrapvst, "MZ..");
	r = eqapo::resolvePluginPath(ws(nowrapvst));
	assert(r.kind == eqapo::PluginKind::WindowsVST3);
	assert(r.path.empty() && !r.hint.empty());

	// Native ELF .so -> VST2, unchanged.
	fs::path real = tmp / "real.so";
	writeBytes(real, std::string("\x7f""ELF....", 8));
	r = eqapo::resolvePluginPath(ws(real));
	assert(r.kind == eqapo::PluginKind::NativeVST2 && r.path == ws(real));

	// Native .vst3 bundle directory -> VST3.
	fs::path bundle = tmp / "Native.vst3";
	fs::create_directories(bundle);
	r = eqapo::resolvePluginPath(ws(bundle));
	assert(r.kind == eqapo::PluginKind::NativeVST3 && r.path == ws(bundle));

	// Missing file.
	r = eqapo::resolvePluginPath(ws(tmp / "missing.dll"));
	assert(r.kind == eqapo::PluginKind::Missing && r.path.empty());

	// Pure mapper, no filesystem needed.
	assert(eqapo::yabridgeWrapperFor(L"C:\\VST\\Foo.dll", "/home/u")
		== L"/home/u/.vst/yabridge/Foo.so");
	assert(eqapo::yabridgeWrapperFor(L"/vst/Bar.vst3", "/home/u")
		== L"/home/u/.vst3/yabridge/Bar.vst3");

	fs::remove_all(tmp);
	std::printf("test_vst_resolve: all checks passed\n");
	return 0;
}
