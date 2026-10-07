/*
    This file is part of Equalizer APO, a system-wide equalizer.

    Linux-only test VST3: a minimal stereo gain effect (fixed +0.5 default,
    one automatable "Gain" parameter) used by vst3_e2e. Implements the VST3
    interfaces directly against the vendored pluginterfaces headers; no SDK
    sources. Not part of the Windows build.
*/

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/ivstplugview.h"
#include "pluginterfaces/vst/vstspeaker.h"

namespace
{
using namespace Steinberg;
using namespace Steinberg::Vst;

const char kEffectCID[16] = {
	(char)0x12, (char)0x34, (char)0x56, (char)0x78,
	(char)0x12, (char)0x34, (char)0x12, (char)0x34,
	(char)0x12, (char)0x34, (char)0x56, (char)0x78,
	(char)0x9A, (char)0xBC, (char)0xDE, (char)0xF0};

class RefCounted
{
public:
	RefCounted() : refs(1) {}
	uint32 addRefImpl() { return ++refs; }
	uint32 releaseImpl()
	{
		uint32 r = --refs;
		if (r == 0)
			delete this;
		return r;
	}

protected:
	virtual ~RefCounted() = default;

private:
	std::atomic<uint32> refs;
};

bool iidEqual(const TUID a, const FUID& b)
{
	// FUID carries a vtable; compare only the 16 UID bytes.
	const TUID& bdata = b;
	return FUnknownPrivate::iidEqual(a, bdata);
}

void copyToString128(const char* ascii, String128 out)
{
	int i = 0;
	for (; ascii[i] != '\0' && i < 127; i++)
		out[i] = (char16_t)ascii[i];
	out[i] = 0;
}

void setGainFromStream(float& gain, IBStream* state)
{
	if (!state)
		return;
	state->seek(0, IBStream::kIBSeekSet, nullptr);
	float g = 0.0f;
	int32 nread = 0;
	if (state->read(&g, (int32)sizeof(g), &nread) == kResultOk && nread == (int32)sizeof(g))
		gain = g;
}

void writeGainToStream(float gain, IBStream* state)
{
	if (!state)
		return;
	int32 nwritten = 0;
	state->write(&gain, (int32)sizeof(gain), &nwritten);
}

// kSpeakerL | kSpeakerR (avoids the overloaded kStereo name).
const SpeakerArrangement kStereoArrangement = (SpeakerArrangement)(kSpeakerL | kSpeakerR);

// Stereo gain: IComponent + IAudioProcessor + IEditController in one object,
// like many simple VST3 effects.
class GainEffect : public RefCounted, public IComponent, public IAudioProcessor, public IEditController
{
public:
	uint32 PLUGIN_API addRef() override { return addRefImpl(); }
	uint32 PLUGIN_API release() override { return releaseImpl(); }
	tresult PLUGIN_API queryInterface(const TUID _iid, void** obj) override
	{
		if (!obj)
			return kInvalidArgument;
		if (iidEqual(_iid, FUnknown::iid) || iidEqual(_iid, IPluginBase::iid) ||
			iidEqual(_iid, IComponent::iid))
			*obj = static_cast<IComponent*>(this);
		else if (iidEqual(_iid, IAudioProcessor::iid))
			*obj = static_cast<IAudioProcessor*>(this);
		else if (iidEqual(_iid, IEditController::iid))
			*obj = static_cast<IEditController*>(this);
		else
		{
			*obj = nullptr;
			return kNoInterface;
		}
		addRef();
		return kResultOk;
	}

	// IPluginBase
	tresult PLUGIN_API initialize(FUnknown*) override { return kResultOk; }
	tresult PLUGIN_API terminate() override { return kResultOk; }

	// IComponent
	tresult PLUGIN_API getControllerClassId(TUID classId) override
	{
		std::memcpy(classId, kEffectCID, sizeof(kEffectCID));
		return kResultOk;
	}
	tresult PLUGIN_API setIoMode(IoMode) override { return kResultOk; }
	int32 PLUGIN_API getBusCount(MediaType type, BusDirection) override
	{
		return type == kAudio ? 1 : 0;
	}
	tresult PLUGIN_API getBusInfo(MediaType type, BusDirection dir, int32 index, BusInfo& bus) override
	{
		if (type != kAudio || index != 0)
			return kResultFalse;
		bus.mediaType = type;
		bus.direction = dir;
		bus.channelCount = 2;
		copyToString128("Stereo", bus.name);
		bus.busType = kMain;
		bus.flags = BusInfo::kDefaultActive;
		return kResultOk;
	}
	tresult PLUGIN_API getRoutingInfo(RoutingInfo&, RoutingInfo&) override { return kNotImplemented; }
	tresult PLUGIN_API activateBus(MediaType, BusDirection, int32, TBool) override { return kResultOk; }
	tresult PLUGIN_API setActive(TBool state) override
	{
		active = state != 0;
		return kResultOk;
	}
	tresult PLUGIN_API setState(IBStream* state) override
	{
		setGainFromStream(gain, state);
		return kResultOk;
	}
	tresult PLUGIN_API getState(IBStream* state) override
	{
		writeGainToStream(gain, state);
		return kResultOk;
	}

	// IAudioProcessor
	tresult PLUGIN_API setBusArrangements(SpeakerArrangement* inputs, int32 numIns,
		SpeakerArrangement* outputs, int32 numOuts) override
	{
		if (numIns != 1 || numOuts != 1 || inputs[0] != kStereoArrangement ||
			outputs[0] != kStereoArrangement)
			return kResultFalse;
		return kResultOk;
	}
	tresult PLUGIN_API getBusArrangement(BusDirection, int32, SpeakerArrangement& arr) override
	{
		arr = kStereoArrangement;
		return kResultOk;
	}
	tresult PLUGIN_API canProcessSampleSize(int32 symbolicSampleSize) override
	{
		return symbolicSampleSize == kSample32 ? kResultOk : kResultFalse;
	}
	uint32 PLUGIN_API getLatencySamples() override { return 0; }
	tresult PLUGIN_API setupProcessing(ProcessSetup& setup) override
	{
		sampleRate = (float)setup.sampleRate;
		return kResultOk;
	}
	tresult PLUGIN_API setProcessing(TBool state) override
	{
		processing = state != 0;
		return kResultOk;
	}
	tresult PLUGIN_API process(ProcessData& data) override
	{
		if (data.symbolicSampleSize != kSample32 || data.numInputs < 1 || data.numOutputs < 1 ||
			!data.inputs || !data.outputs)
			return kResultFalse;
		AudioBusBuffers& in = data.inputs[0];
		AudioBusBuffers& out = data.outputs[0];
		if (!in.channelBuffers32 || !out.channelBuffers32)
			return kResultFalse;
		const int32 ch = in.numChannels < out.numChannels ? in.numChannels : out.numChannels;
		for (int32 c = 0; c < ch; c++)
		{
			if (in.silenceFlags & ((uint64)1 << (uint64)c))
			{
				std::memset(out.channelBuffers32[c], 0, (size_t)data.numSamples * sizeof(float));
				out.silenceFlags |= ((uint64)1 << (uint64)c);
			}
			else
			{
				for (int32 i = 0; i < data.numSamples; i++)
					out.channelBuffers32[c][i] = in.channelBuffers32[c][i] * gain;
				out.silenceFlags &= ~((uint64)1 << (uint64)c);
			}
		}
		return kResultOk;
	}
	uint32 PLUGIN_API getTailSamples() override { return 0; }

	// IEditController (setState/getState are shared with IComponent above)
	tresult PLUGIN_API setComponentState(IBStream* state) override
	{
		setGainFromStream(gain, state);
		return kResultOk;
	}
	int32 PLUGIN_API getParameterCount() override { return 1; }
	tresult PLUGIN_API getParameterInfo(int32 paramIndex, ParameterInfo& info) override
	{
		if (paramIndex != 0)
			return kResultFalse;
		info.id = 0;
		copyToString128("Gain", info.title);
		copyToString128("Gain", info.shortTitle);
		copyToString128("", info.units);
		info.stepCount = 0;
		info.defaultNormalizedValue = 0.5;
		info.unitId = 0;
		info.flags = ParameterInfo::kCanAutomate;
		return kResultOk;
	}
	tresult PLUGIN_API getParamStringByValue(ParamID, ParamValue valueNormalized, String128 string) override
	{
		char tmp[32];
		std::snprintf(tmp, sizeof(tmp), "%.3f", valueNormalized);
		copyToString128(tmp, string);
		return kResultOk;
	}
	tresult PLUGIN_API getParamValueByString(ParamID, TChar* string, ParamValue& valueNormalized) override
	{
		char tmp[128];
		int i = 0;
		for (; string[i] != 0 && i < 127; i++)
			tmp[i] = (char)string[i];
		tmp[i] = '\0';
		valueNormalized = std::atof(tmp);
		return kResultOk;
	}
	ParamValue PLUGIN_API normalizedParamToPlain(ParamID, ParamValue valueNormalized) override
	{
		return valueNormalized;
	}
	ParamValue PLUGIN_API plainParamToNormalized(ParamID, ParamValue plainValue) override
	{
		return plainValue;
	}
	ParamValue PLUGIN_API getParamNormalized(ParamID id) override
	{
		return id == 0 ? (ParamValue)gain : 0.0;
	}
	tresult PLUGIN_API setParamNormalized(ParamID id, ParamValue value) override
	{
		if (id != 0)
			return kResultFalse;
		gain = (float)value;
		return kResultOk;
	}
	tresult PLUGIN_API setComponentHandler(IComponentHandler*) override { return kResultOk; }
	IPlugView* PLUGIN_API createView(FIDString) override { return nullptr; }

private:
	float gain = 0.5f;
	float sampleRate = 48000.0f;
	bool active = false;
	bool processing = false;
};

class StubFactory : public RefCounted, public IPluginFactory
{
public:
	uint32 PLUGIN_API addRef() override { return addRefImpl(); }
	uint32 PLUGIN_API release() override { return releaseImpl(); }
	tresult PLUGIN_API queryInterface(const TUID _iid, void** obj) override
	{
		if (!obj)
			return kInvalidArgument;
		if (iidEqual(_iid, FUnknown::iid) || iidEqual(_iid, IPluginBase::iid) ||
			iidEqual(_iid, IPluginFactory::iid))
			*obj = static_cast<IPluginFactory*>(this);
		else
		{
			*obj = nullptr;
			return kNoInterface;
		}
		addRef();
		return kResultOk;
	}
	tresult PLUGIN_API getFactoryInfo(PFactoryInfo* info) override
	{
		if (!info)
			return kInvalidArgument;
		std::memset(info, 0, sizeof(*info));
		std::strncpy(info->vendor, "EqapoTest", sizeof(info->vendor) - 1);
		return kResultOk;
	}
	int32 PLUGIN_API countClasses() override { return 1; }
	tresult PLUGIN_API getClassInfo(int32 index, PClassInfo* info) override
	{
		if (!info || index != 0)
			return kInvalidArgument;
		std::memset(info, 0, sizeof(*info));
		std::memcpy(info->cid, kEffectCID, sizeof(kEffectCID));
		info->cardinality = PClassInfo::kManyInstances;
		std::strncpy(info->category, kVstAudioEffectClass, sizeof(info->category) - 1);
		std::strncpy(info->name, "EqapoVST3Stub", sizeof(info->name) - 1);
		return kResultOk;
	}
	tresult PLUGIN_API createInstance(FIDString cid, FIDString _iid, void** obj) override
	{
		if (!obj || std::memcmp(cid, kEffectCID, sizeof(kEffectCID)) != 0)
		{
			if (obj)
				*obj = nullptr;
			return kNoInterface;
		}
		GainEffect* effect = new GainEffect();
		tresult res = effect->queryInterface(_iid, obj);
		effect->release();
		return res;
	}
};
}

extern "C" Steinberg::IPluginFactory* GetPluginFactory()
{
	static Steinberg::IPluginFactory* factory = nullptr;
	if (!factory)
		factory = new StubFactory();
	return factory;
}
