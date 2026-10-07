/*
    This file is part of Equalizer APO, a system-wide equalizer.

    Linux-only native VST3 filter (stereo float32). Not part of the Windows
    build. Mirrors VSTPluginFilter's structure; VST3 processing is float-only,
    so doubles convert at the boundary like the VST2 float fallback.
*/

#pragma once

#include "IFilter.h"
#include "helpers/VST3PluginLibrary.h"

class VST3PluginInstance;

class VST3PluginFilter : public IFilter
{
public:
	VST3PluginFilter(std::shared_ptr<VST3PluginLibrary> library, std::wstring chunkData, std::unordered_map<std::wstring, float> paramMap);
	~VST3PluginFilter();

	bool getInPlace() override {return false;}
	std::vector<std::wstring> initialize(float sampleRate, unsigned maxFrameCount, std::vector<std::wstring> channelNames) override;
	void process(double** output, double** input, unsigned frameCount) override;

	std::shared_ptr<VST3PluginLibrary> getLibrary() const;
	std::wstring getChunkData() const;
	std::unordered_map<std::wstring, float> getParamMap() const;

private:
	void cleanup();
	void prepare(float sampleRate, unsigned maxFrameCount);

	std::shared_ptr<VST3PluginLibrary> library;
	std::wstring libPath;
	std::wstring chunkData;
	std::unordered_map<std::wstring, float> paramMap;
	size_t channelCount = 0;

	VST3PluginInstance* effect = nullptr;

	float** floatInputs = nullptr;
	float* floatInBuf = nullptr;
	float** floatOutputs = nullptr;
	float* floatOutBuf = nullptr;
	unsigned maxFrames = 0;

	unsigned delayLength = 0;
	double** delayBufs = nullptr;
	unsigned delayOff = 0;

	bool skipProcessing = false;
};
