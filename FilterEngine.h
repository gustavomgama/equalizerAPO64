/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2014  Jonas Thedering

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

#pragma once

#include <string>
#include <vector>
#include <unordered_set>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>

// Portable binary semaphore replacing the Windows semaphore that prevents
// reloading the configuration while a transition is still running.
class FilterEngineSemaphore
{
public:
	void release()
	{
		{
			std::lock_guard<std::mutex> lock(mutex);
			count = 1;
		}
		condition.notify_all();
	}

	void stop()
	{
		{
			std::lock_guard<std::mutex> lock(mutex);
			stopped = true;
		}
		condition.notify_all();
	}

	// Blocks until released. Returns false if stop() was called first.
	bool waitOrStop()
	{
		std::unique_lock<std::mutex> lock(mutex);
		condition.wait(lock, [this] { return count > 0 || stopped; });
		if (stopped)
			return false;
		count = 0;
		return true;
	}

private:
	std::mutex mutex;
	std::condition_variable condition;
	int count = 1;
	bool stopped = false;
};
#endif

#include "IFilterFactory.h"
#include "FilterConfiguration.h"
#include "helpers/PrecisionTimer.h"
#include "helpers/MemoryHelper.h"

namespace mup {
class ParserX;
}

#pragma AVRT_VTABLES_BEGIN
class FilterEngine
{
public:
	FilterEngine();
	~FilterEngine();

	void setPreMix(bool preMix);
	void setDeviceInfo(bool capture, bool postMixInstalled, const std::wstring& deviceName, const std::wstring& connectionName, const std::wstring& deviceGuid, const std::wstring& deviceString);
	void initialize(float sampleRate, unsigned inputChannelCount, unsigned realChannelCount, unsigned outputChannelCount, unsigned channelMask, unsigned maxFrameCount, const std::wstring& customPath = L"");
	void loadConfig(const std::wstring& customPath = L"");
	void loadConfigFile(const std::wstring& path);
	void watchRegistryKey(const std::wstring& key);
	void process(float* output, float* input, unsigned frameCount);
	void process(float** output, float** input, unsigned frameCount);
	void process(double* output, double* input, unsigned frameCount);
	void process(double** output, double** input, unsigned frameCount);

	bool isPreMix() const {return preMix;}
	bool isCapture() const {return capture;}
	bool isPostMixInstalled() const {return postMixInstalled;}
	std::wstring getDeviceName() const {return deviceName;}
	std::wstring getConnectionName() const {return connectionName;}
	std::wstring getDeviceGuid() const {return deviceGuid;}
	std::wstring getDeviceString() const {return deviceString;}
	unsigned getInputChannelCount() const {return inputChannelCount;}
	unsigned getRealChannelCount() const {return realChannelCount;}
	unsigned getOutputChannelCount() const {return outputChannelCount;}
	unsigned getChannelMask() const {return channelMask;}
	float getSampleRate() const {return sampleRate;}
	unsigned getMaxFrameCount() const {return maxFrameCount;}
	mup::ParserX* getParser() {return parser;}

private:
	void addFilters(std::vector<IFilter*> filters);
	void cleanupConfigurations();
#ifdef _WIN32
	static unsigned long __stdcall notificationThread(void* parameter);
#else
	static void notificationThread(FilterEngine* engine);
#endif
	void resizeBuffers(unsigned frameCount);

	std::vector<IFilterFactory*> factories;

	std::vector<std::unique_ptr<double[]>> inputBuf2D, outputBuf2D;
	std::vector<double> inputBuf1D, outputBuf1D;
	unsigned allocatedFrameCount;

	bool preMix;
	bool capture;
	bool postMixInstalled;
	std::wstring deviceName;
	std::wstring connectionName;
	std::wstring deviceGuid;
	std::wstring deviceString;
	std::wstring configPath;
	float sampleRate;
	// number of input channels that originally existed before child APO processing
	unsigned inputChannelCount;
	// number of input channels in process (including channels coming from child APO output)
	unsigned realChannelCount;
	unsigned outputChannelCount;
	unsigned channelMask;
	unsigned maxFrameCount;

	// only used during loading
	std::vector<FilterInfo*> filterInfos;
	std::vector<std::wstring> currentChannelNames;
	std::vector<std::wstring> lastChannelNames;
	std::vector<std::wstring> lastNewChannelNames;
	std::vector<std::wstring> allChannelNames;
	bool lastInPlace;
	mup::ParserX* parser;

	FilterConfiguration* currentConfig;
	FilterConfiguration* nextConfig;
	FilterConfiguration* previousConfig;

	unsigned transitionCounter;
	unsigned transitionLength;
	PrecisionTimer timer;
#ifdef _WIN32
	HANDLE loadSemaphore;
	CRITICAL_SECTION loadSection;
	void* threadHandle;
	void* shutdownEvent;
#else
	std::recursive_mutex loadSection;
	FilterEngineSemaphore loadSemaphore;
	std::thread notificationThreadObj;
	std::atomic<bool> notificationShutdown{false};
#endif
	std::unordered_set<std::wstring> watchRegistryKeys;
	bool lastInputWasSilent;
};
#pragma AVRT_VTABLES_END
