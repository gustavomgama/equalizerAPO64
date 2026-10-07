/*
    This file is part of Equalizer APO, a system-wide equalizer.

    Linux-only native VST3 filter (stereo float32). Not part of the Windows
    build.
*/

#include "VST3PluginFilter.h"

#include <cstring>

#include "helpers/LogHelper.h"
#include "helpers/VST3PluginInstance.h"

using namespace std;

VST3PluginFilter::VST3PluginFilter(std::shared_ptr<VST3PluginLibrary> library,
	std::wstring chunkData, std::unordered_map<std::wstring, float> paramMap)
	: library(library), chunkData(chunkData), paramMap(paramMap)
{
	libPath = library->getLibPath();
}

VST3PluginFilter::~VST3PluginFilter()
{
	cleanup();
}

std::vector<std::wstring> VST3PluginFilter::initialize(float sampleRate,
	unsigned maxFrameCount, std::vector<std::wstring> channelNames)
{
	cleanup();

	channelCount = channelNames.size();
	if (channelCount != 2)
	{
		LogF(L"VST3 plugin %s needs stereo (got %d channels); passing through",
			libPath.c_str(), (int)channelCount);
		skipProcessing = true;
		return channelNames;
	}

	skipProcessing = false;
	maxFrames = maxFrameCount;

	effect = new VST3PluginInstance(library, 2);
	if (!effect->initialize() || effect->numInputs() != 2 || effect->numOutputs() != 2)
	{
		LogF(L"The VST3 plugin %s could not be initialized.", libPath.c_str());
		skipProcessing = true;
	}

	prepare(sampleRate, maxFrameCount);

	floatInBuf = new float[2 * maxFrameCount]();
	floatOutBuf = new float[2 * maxFrameCount]();
	floatInputs = new float*[2];
	floatOutputs = new float*[2];
	for (int c = 0; c < 2; c++)
	{
		floatInputs[c] = floatInBuf + (size_t)c * maxFrameCount;
		floatOutputs[c] = floatOutBuf + (size_t)c * maxFrameCount;
	}

	const unsigned latency = effect != nullptr ? (unsigned)effect->getInitialDelay() : 0;
	delayLength = latency;
	delayOff = 0;
	if (delayLength > 0)
	{
		delayBufs = new double*[2];
		for (int c = 0; c < 2; c++)
		{
			delayBufs[c] = new double[delayLength]();
			std::memset(delayBufs[c], 0, delayLength * sizeof(double));
		}
	}

	return channelNames;
}

void VST3PluginFilter::prepare(float sampleRate, unsigned maxFrameCount)
{
	if (effect == nullptr)
		return;
	effect->prepareForProcessing(sampleRate, (int)maxFrameCount);
	effect->writeToEffect(chunkData, paramMap);
	effect->startProcessing();
}

void VST3PluginFilter::process(double** output, double** input, unsigned frameCount)
{
	if (skipProcessing || effect == nullptr)
	{
		for (size_t i = 0; i < channelCount; i++)
			memcpy(output[i], input[i], frameCount * sizeof(double));
		return;
	}

	for (int c = 0; c < 2; c++)
		for (unsigned i = 0; i < frameCount; i++)
			floatInputs[c][i] = (float)input[c][i];

	if (!effect->process(floatInputs, floatOutputs, (int)frameCount))
	{
		for (size_t i = 0; i < channelCount; i++)
			memcpy(output[i], input[i], frameCount * sizeof(double));
		return;
	}

	for (int c = 0; c < 2; c++)
		for (unsigned i = 0; i < frameCount; i++)
			output[c][i] = (double)floatOutputs[c][i];

	// Delay compensation: shift the output by the plugin latency.
	if (delayLength > 0 && delayBufs != nullptr)
	{
		for (unsigned i = 0; i < frameCount; i++)
		{
			for (int c = 0; c < 2; c++)
			{
				const double tmp = output[c][i];
				output[c][i] = delayBufs[c][delayOff];
				delayBufs[c][delayOff] = tmp;
			}
			delayOff = (delayOff + 1) % delayLength;
		}
	}
}

std::shared_ptr<VST3PluginLibrary> VST3PluginFilter::getLibrary() const
{
	return library;
}

std::wstring VST3PluginFilter::getChunkData() const
{
	return chunkData;
}

std::unordered_map<std::wstring, float> VST3PluginFilter::getParamMap() const
{
	return paramMap;
}

void VST3PluginFilter::cleanup()
{
	if (effect != nullptr)
	{
		effect->stopProcessing();
		delete effect;
		effect = nullptr;
	}
	delete[] floatInputs;
	floatInputs = nullptr;
	delete[] floatInBuf;
	floatInBuf = nullptr;
	delete[] floatOutputs;
	floatOutputs = nullptr;
	delete[] floatOutBuf;
	floatOutBuf = nullptr;
	if (delayBufs != nullptr)
	{
		for (int c = 0; c < 2; c++)
			delete[] delayBufs[c];
		delete[] delayBufs;
		delayBufs = nullptr;
	}
	delayLength = 0;
	delayOff = 0;
}
