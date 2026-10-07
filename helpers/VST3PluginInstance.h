/*
    This file is part of Equalizer APO, a system-wide equalizer.

    Linux-only native VST3 effect instance (IComponent/IAudioProcessor/
    IEditController driver). Not part of the Windows build. Runs float32
    (all VST3 processors support it; 64-bit is optional) and maps state and
    parameters onto the same ChunkData/paramMap schema as VST2.
*/

#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

class VST3PluginLibrary;

class VST3PluginInstance
{
public:
	VST3PluginInstance(const std::shared_ptr<VST3PluginLibrary>& library, int processLevel);
	~VST3PluginInstance();

	bool initialize();

	// Stereo effect; 0 when not initialized.
	int numInputs() const;
	int numOutputs() const;
	bool canReplacing() const;
	bool canDoubleReplacing() const;
	int getUsedChannelCount() const;
	void setUsedChannelCount(int count);
	float getSampleRate() const;
	int getProcessLevel() const;
	void setProcessLevel(int value);
	int getInitialDelay() const;
	std::wstring getName() const;

	void prepareForProcessing(float sampleRate, int blockSize);
	void writeToEffect(const std::wstring& chunkData, const std::unordered_map<std::wstring, float>& paramMap);
	void readFromEffect(std::wstring& chunkData, std::unordered_map<std::wstring, float>& paramMap);
	void startProcessing();
	bool process(float** inputArray, float** outputArray, int frameCount);
	void stopProcessing();

private:
	std::shared_ptr<VST3PluginLibrary> library;
	Steinberg::Vst::IComponent* component = nullptr;
	Steinberg::Vst::IAudioProcessor* processor = nullptr;
	Steinberg::Vst::IEditController* controller = nullptr;
	Steinberg::Vst::IHostApplication* hostApp = nullptr;
	Steinberg::Vst::IComponentHandler* handler = nullptr;
	Steinberg::Vst::ProcessContext context{};
	float sampleRate = 0.0f;
	int usedChannelCount = -1;
	int processLevel = 0;
	bool controllerIsComponent = false;
};
