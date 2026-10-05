/*
    This file is part of Equalizer APO, a system-wide equalizer.

    Linux-only WAV-in/WAV-out host used to verify the DSP engine with no audio
    server. It is not part of the Windows build.
*/

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <sndfile.h>
#include "FilterEngine.h"
#include "helpers/ConfigPathHelper.h"

int main(int argc, char** argv)
{
	std::string inPath, outPath, configPath;
	for (int i = 1; i < argc; ++i)
	{
		if (!strcmp(argv[i], "--in") && i + 1 < argc) inPath = argv[++i];
		else if (!strcmp(argv[i], "--out") && i + 1 < argc) outPath = argv[++i];
		else if (!strcmp(argv[i], "--config") && i + 1 < argc) configPath = argv[++i];
	}
	if (inPath.empty() || outPath.empty())
	{
		std::fprintf(stderr, "usage: eqapo-null --in <wav> --out <wav> [--config <file>]\n");
		return 2;
	}

	SF_INFO inInfo{};
	SNDFILE* in = sf_open(inPath.c_str(), SFM_READ, &inInfo);
	if (!in)
	{
		std::fprintf(stderr, "open %s: %s\n", inPath.c_str(), sf_strerror(nullptr));
		return 1;
	}

	SF_INFO outInfo = inInfo;
	SNDFILE* out = sf_open(outPath.c_str(), SFM_WRITE, &outInfo);
	if (!out)
	{
		std::fprintf(stderr, "create %s: %s\n", outPath.c_str(), sf_strerror(nullptr));
		sf_close(in);
		return 1;
	}

	// Use an explicit config file so the engine does not start a watcher.
	std::wstring cfg;
	if (!configPath.empty())
		cfg.assign(configPath.begin(), configPath.end());
	else
		cfg = ConfigPathHelper::getConfigFile();

	const unsigned maxFrames = 1024;
	FilterEngine engine;
	// Give the engine a device identity so `Device:` blocks can match on Linux
	// (use `Device: all` or `Device: EqualizerAPO`).
	engine.setDeviceInfo(false, true, L"EqualizerAPO", L"", L"", L"EqualizerAPO");
	engine.initialize((float)inInfo.samplerate, inInfo.channels, inInfo.channels,
		inInfo.channels, 0, maxFrames, cfg);

	std::vector<double> inBuf(maxFrames * inInfo.channels);
	std::vector<double> outBuf(maxFrames * inInfo.channels);
	sf_count_t frames;
	while ((frames = sf_readf_double(in, inBuf.data(), maxFrames)) > 0)
	{
		engine.process(outBuf.data(), inBuf.data(), (unsigned)frames);
		if (sf_writef_double(out, outBuf.data(), frames) != frames)
		{
			std::fprintf(stderr, "write %s: %s\n", outPath.c_str(), sf_strerror(out));
			sf_close(in);
			sf_close(out);
			return 1;
		}
	}

	sf_close(in);
	sf_close(out);
	return 0;
}
