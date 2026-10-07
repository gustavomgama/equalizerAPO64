/*
    This file is part of Equalizer APO, a system-wide equalizer.

    Linux-only native VST3 effect instance. Not part of the Windows build.
*/

#include "VST3PluginInstance.h"

#include <atomic>
#include <cstring>
#include <vector>

#include "VST3PluginLibrary.h"
#include "helpers/LogHelper.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/vstspeaker.h"

using namespace std;
namespace vst = Steinberg::Vst;

// Portable base64 for VST3 component state (mirrors the VST2 chunk handling).
namespace
{
const char* kBase64Chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64Encode(const unsigned char* data, size_t len)
{
	std::string out;
	int val = 0, bits = -6;
	for (size_t i = 0; i < len; ++i)
	{
		val = (val << 8) + data[i];
		bits += 8;
		while (bits >= 0)
		{
			out.push_back(kBase64Chars[(val >> bits) & 0x3F]);
			bits -= 6;
		}
	}
	if (bits > -6)
		out.push_back(kBase64Chars[((val << 8) >> (bits + 8)) & 0x3F]);
	while (out.size() % 4)
		out.push_back('=');
	return out;
}

std::vector<unsigned char> base64Decode(const std::string& s)
{
	auto value = [](char c) -> int {
		if (c >= 'A' && c <= 'Z') return c - 'A';
		if (c >= 'a' && c <= 'z') return c - 'a' + 26;
		if (c >= '0' && c <= '9') return c - '0' + 52;
		if (c == '+') return 62;
		if (c == '/') return 63;
		return -1;
	};
	std::vector<unsigned char> out;
	int val = 0, bits = -8;
	for (char c : s)
	{
		int d = value(c);
		if (d < 0)
			continue;
		val = (val << 6) + d;
		bits += 6;
		if (bits >= 0)
		{
			out.push_back((unsigned char)((val >> bits) & 0xFF));
			bits -= 8;
		}
	}
	return out;
}

// Non-FUnknown refcount storage; each host class below has exactly one
// FUnknown base (from its VST interface), so there is no diamond.
class RefCounted
{
public:
	RefCounted() : refs(1) {}
	Steinberg::uint32 addRefImpl() { return ++refs; }
	Steinberg::uint32 releaseImpl()
	{
		Steinberg::uint32 r = --refs;
		if (r == 0)
			delete this;
		return r;
	}

protected:
	virtual ~RefCounted() = default;

private:
	std::atomic<Steinberg::uint32> refs;
};

bool iidEqual(const Steinberg::TUID a, const Steinberg::FUID& b)
{
	// FUID carries a vtable; compare only the 16 UID bytes.
	const Steinberg::TUID& bdata = b;
	return Steinberg::FUnknownPrivate::iidEqual(a, bdata);
}

// kSpeakerL | kSpeakerR (avoids the overloaded kStereo name).
const vst::SpeakerArrangement kStereoArrangement =
	(vst::SpeakerArrangement)(vst::kSpeakerL | vst::kSpeakerR);

std::wstring toWString(const vst::String128 s)
{
	std::wstring out;
	for (int i = 0; i < 128 && s[i] != 0; i++)
		out.push_back((wchar_t)s[i]);
	return out;
}

class HostApp : public RefCounted, public vst::IHostApplication
{
public:
	Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID _iid, void** obj) override
	{
		if (!obj)
			return Steinberg::kInvalidArgument;
		if (iidEqual(_iid, Steinberg::FUnknown::iid) || iidEqual(_iid, vst::IHostApplication::iid))
		{
			*obj = static_cast<vst::IHostApplication*>(this);
			addRef();
			return Steinberg::kResultOk;
		}
		*obj = nullptr;
		return Steinberg::kNoInterface;
	}
	Steinberg::uint32 PLUGIN_API addRef() override { return addRefImpl(); }
	Steinberg::uint32 PLUGIN_API release() override { return releaseImpl(); }
	Steinberg::tresult PLUGIN_API getName(vst::String128 name) override
	{
		const char* ascii = "EqualizerAPO";
		int i = 0;
		for (; ascii[i] != '\0' && i < 127; i++)
			name[i] = (char16_t)ascii[i];
		name[i] = 0;
		return Steinberg::kResultOk;
	}
	Steinberg::tresult PLUGIN_API createInstance(Steinberg::TUID, Steinberg::TUID, void** obj) override
	{
		if (obj)
			*obj = nullptr;
		return Steinberg::kNotImplemented;
	}
};

class ComponentHandler : public RefCounted, public vst::IComponentHandler
{
public:
	Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID _iid, void** obj) override
	{
		if (!obj)
			return Steinberg::kInvalidArgument;
		if (iidEqual(_iid, Steinberg::FUnknown::iid) || iidEqual(_iid, vst::IComponentHandler::iid))
		{
			*obj = static_cast<vst::IComponentHandler*>(this);
			addRef();
			return Steinberg::kResultOk;
		}
		*obj = nullptr;
		return Steinberg::kNoInterface;
	}
	Steinberg::uint32 PLUGIN_API addRef() override { return addRefImpl(); }
	Steinberg::uint32 PLUGIN_API release() override { return releaseImpl(); }
	Steinberg::tresult PLUGIN_API beginEdit(vst::ParamID) override { return Steinberg::kResultOk; }
	Steinberg::tresult PLUGIN_API performEdit(vst::ParamID, vst::ParamValue) override { return Steinberg::kResultOk; }
	Steinberg::tresult PLUGIN_API endEdit(vst::ParamID) override { return Steinberg::kResultOk; }
	Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32) override { return Steinberg::kResultOk; }
};

class MemoryStream : public RefCounted, public Steinberg::IBStream
{
public:
	Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID _iid, void** obj) override
	{
		if (!obj)
			return Steinberg::kInvalidArgument;
		if (iidEqual(_iid, Steinberg::FUnknown::iid) || iidEqual(_iid, Steinberg::IBStream::iid))
		{
			*obj = static_cast<Steinberg::IBStream*>(this);
			addRef();
			return Steinberg::kResultOk;
		}
		*obj = nullptr;
		return Steinberg::kNoInterface;
	}
	Steinberg::uint32 PLUGIN_API addRef() override { return addRefImpl(); }
	Steinberg::uint32 PLUGIN_API release() override { return releaseImpl(); }
	Steinberg::tresult PLUGIN_API read(void* buffer, Steinberg::int32 numBytes, Steinberg::int32* numBytesRead) override
	{
		Steinberg::int64 avail = (Steinberg::int64)data.size() - pos;
		Steinberg::int32 n = numBytes < avail ? numBytes : (Steinberg::int32)avail;
		if (n > 0)
			std::memcpy(buffer, data.data() + pos, (size_t)n);
		pos += n;
		if (numBytesRead)
			*numBytesRead = n;
		return Steinberg::kResultOk;
	}
	Steinberg::tresult PLUGIN_API write(void* buffer, Steinberg::int32 numBytes, Steinberg::int32* numBytesWritten) override
	{
		if (pos + numBytes > (Steinberg::int64)data.size())
			data.resize((size_t)(pos + numBytes));
		std::memcpy(data.data() + pos, buffer, (size_t)numBytes);
		pos += numBytes;
		if (numBytesWritten)
			*numBytesWritten = numBytes;
		return Steinberg::kResultOk;
	}
	Steinberg::tresult PLUGIN_API seek(Steinberg::int64 seekPos, Steinberg::int32 mode, Steinberg::int64* result) override
	{
		Steinberg::int64 base = 0;
		if (mode == Steinberg::IBStream::kIBSeekCur)
			base = pos;
		else if (mode == Steinberg::IBStream::kIBSeekEnd)
			base = (Steinberg::int64)data.size();
		pos = base + seekPos;
		if (pos < 0)
			pos = 0;
		if (result)
			*result = pos;
		return Steinberg::kResultOk;
	}
	Steinberg::tresult PLUGIN_API tell(Steinberg::int64* posOut) override
	{
		if (!posOut)
			return Steinberg::kInvalidArgument;
		*posOut = pos;
		return Steinberg::kResultOk;
	}
	void setData(std::vector<char>&& d)
	{
		data = std::move(d);
		pos = 0;
	}
	const std::vector<char>& bytes() const { return data; }

private:
	std::vector<char> data;
	Steinberg::int64 pos = 0;
};
}

VST3PluginInstance::VST3PluginInstance(const std::shared_ptr<VST3PluginLibrary>& library, int processLevel)
	: library(library), processLevel(processLevel)
{
	std::memset(&context, 0, sizeof(context));
}

VST3PluginInstance::~VST3PluginInstance()
{
	stopProcessing();
	if (component != nullptr)
	{
		component->setActive(false);
		component->terminate();
		component->release();
		component = nullptr;
	}
	if (controller != nullptr)
	{
		if (!controllerIsComponent)
			controller->terminate();
		controller->release();
		controller = nullptr;
	}
	if (processor != nullptr)
	{
		processor->release();
		processor = nullptr;
	}
	if (handler != nullptr)
	{
		handler->release();
		handler = nullptr;
	}
	if (hostApp != nullptr)
	{
		hostApp->release();
		hostApp = nullptr;
	}
}

static Steinberg::int32 busChannels(Steinberg::Vst::IComponent* component,
	Steinberg::Vst::MediaType type, Steinberg::Vst::BusDirection dir)
{
	if (component->getBusCount(type, dir) < 1)
		return 0;
	Steinberg::Vst::BusInfo info{};
	if (component->getBusInfo(type, dir, 0, info) != Steinberg::kResultOk)
		return 0;
	return info.channelCount;
}

bool VST3PluginInstance::initialize()
{
	component = library->createComponent();
	if (component == nullptr)
		return false;

	hostApp = new HostApp();
	if (component->initialize(hostApp) != Steinberg::kResultOk)
		return false;

	if (component->queryInterface(vst::IAudioProcessor::iid, (void**)&processor) != Steinberg::kResultOk
		|| processor == nullptr)
		return false;

	// The controller is usually the component itself; otherwise instantiate
	// the class it names.
	controllerIsComponent = false;
	if (component->queryInterface(vst::IEditController::iid, (void**)&controller) == Steinberg::kResultOk
		&& controller != nullptr)
		controllerIsComponent = true;
	else
	{
		controller = nullptr;
		Steinberg::TUID controllerCID{};
		if (component->getControllerClassId(controllerCID) == Steinberg::kResultOk)
		{
			bool isNull = true;
			for (char c : controllerCID)
				isNull = isNull && (c == 0);
			if (!isNull && library->factory() != nullptr &&
				library->factory()->createInstance(controllerCID, vst::IEditController::iid,
					(void**)&controller) == Steinberg::kResultOk && controller != nullptr)
				controller->initialize(hostApp);
			else
				controller = nullptr;
		}
	}
	if (controller != nullptr)
	{
		handler = new ComponentHandler();
		controller->setComponentHandler(handler);
	}

	{
		const Steinberg::int32 inCh = busChannels(component, vst::kAudio, vst::kInput);
		const Steinberg::int32 outCh = busChannels(component, vst::kAudio, vst::kOutput);
		// Stereo effect hosting (matches the host's stereo pipeline).
		if (inCh != 2 || outCh != 2)
		{
			LogF(L"VST3 plugin %s has %d in / %d out channels; stereo expected",
				library->getLibPath().c_str(), (int)inCh, (int)outCh);
			return false;
		}
	}

	if (component->setActive(true) != Steinberg::kResultOk)
		return false;

	usedChannelCount = 2;
	return true;
}

int VST3PluginInstance::numInputs() const
{
	return component == nullptr ? 0 : 2;
}

int VST3PluginInstance::numOutputs() const
{
	return component == nullptr ? 0 : 2;
}

bool VST3PluginInstance::canReplacing() const
{
	return true;
}

bool VST3PluginInstance::canDoubleReplacing() const
{
	return false;
}

int VST3PluginInstance::getUsedChannelCount() const
{
	return usedChannelCount;
}

void VST3PluginInstance::setUsedChannelCount(int count)
{
	usedChannelCount = count;
}

float VST3PluginInstance::getSampleRate() const
{
	return sampleRate;
}

int VST3PluginInstance::getProcessLevel() const
{
	return processLevel;
}

void VST3PluginInstance::setProcessLevel(int value)
{
	processLevel = value;
}

int VST3PluginInstance::getInitialDelay() const
{
	if (processor == nullptr)
		return 0;
	return (int)processor->getLatencySamples();
}

std::wstring VST3PluginInstance::getName() const
{
	const std::string& n = library->effectName();
	return std::wstring(n.begin(), n.end());
}

void VST3PluginInstance::prepareForProcessing(float rate, int blockSize)
{
	if (processor == nullptr)
		return;

	sampleRate = rate;
	vst::ProcessSetup setup{};
	setup.processMode = vst::kRealtime;
	setup.symbolicSampleSize = vst::kSample32;
	setup.maxSamplesPerBlock = blockSize;
	setup.sampleRate = rate;
	if (processor->setupProcessing(setup) != Steinberg::kResultOk)
		return;

	vst::SpeakerArrangement inArr = kStereoArrangement;
	vst::SpeakerArrangement outArr = kStereoArrangement;
	processor->setBusArrangements(&inArr, 1, &outArr, 1);
	component->activateBus(vst::kAudio, vst::kInput, 0, true);
	component->activateBus(vst::kAudio, vst::kOutput, 0, true);

	context.sampleRate = rate;
}

void VST3PluginInstance::writeToEffect(const std::wstring& chunkData,
	const std::unordered_map<std::wstring, float>& paramMap)
{
	if (component == nullptr)
		return;

	if (!chunkData.empty())
	{
		const std::string b64(chunkData.begin(), chunkData.end());
		const std::vector<unsigned char> raw = base64Decode(b64);
		if (!raw.empty())
		{
			MemoryStream* stream = new MemoryStream();
			std::vector<char> bytes(raw.begin(), raw.end());
			stream->setData(std::move(bytes));
			component->setState(stream);
			stream->release();
		}
	}

	if (controller == nullptr)
		return;
	const Steinberg::int32 count = controller->getParameterCount();
	for (Steinberg::int32 i = 0; i < count; i++)
	{
		vst::ParameterInfo info{};
		if (controller->getParameterInfo(i, info) != Steinberg::kResultOk)
			continue;
		if (info.flags & vst::ParameterInfo::kIsReadOnly)
			continue;
		auto it = paramMap.find(toWString(info.title));
		if (it != paramMap.end())
			controller->setParamNormalized(info.id, (vst::ParamValue)it->second);
	}
}

void VST3PluginInstance::readFromEffect(std::wstring& chunkData,
	std::unordered_map<std::wstring, float>& paramMap)
{
	chunkData = L"";
	paramMap.clear();
	if (component == nullptr)
		return;

	MemoryStream* stream = new MemoryStream();
	if (component->getState(stream) == Steinberg::kResultOk && !stream->bytes().empty())
	{
		const std::string b64 = base64Encode(
			(const unsigned char*)stream->bytes().data(), stream->bytes().size());
		chunkData.assign(b64.begin(), b64.end());
	}
	stream->release();

	if (controller == nullptr)
		return;
	const Steinberg::int32 count = controller->getParameterCount();
	for (Steinberg::int32 i = 0; i < count; i++)
	{
		vst::ParameterInfo info{};
		if (controller->getParameterInfo(i, info) != Steinberg::kResultOk)
			continue;
		if (!(info.flags & vst::ParameterInfo::kCanAutomate))
			continue;
		paramMap[toWString(info.title)] = (float)controller->getParamNormalized(info.id);
	}
}

void VST3PluginInstance::startProcessing()
{
	if (processor == nullptr)
		return;
	processor->setProcessing(true);
}

bool VST3PluginInstance::process(float** inputArray, float** outputArray, int frameCount)
{
	if (processor == nullptr)
		return false;

	vst::AudioBusBuffers inBus{};
	inBus.numChannels = 2;
	inBus.silenceFlags = 0;
	inBus.channelBuffers32 = inputArray;
	vst::AudioBusBuffers outBus{};
	outBus.numChannels = 2;
	outBus.silenceFlags = 0;
	outBus.channelBuffers32 = outputArray;

	vst::ProcessData data{};
	data.processMode = vst::kRealtime;
	data.symbolicSampleSize = vst::kSample32;
	data.numSamples = frameCount;
	data.numInputs = 1;
	data.numOutputs = 1;
	data.inputs = &inBus;
	data.outputs = &outBus;
	data.processContext = &context;

	return processor->process(data) == Steinberg::kResultOk;
}

void VST3PluginInstance::stopProcessing()
{
	if (processor == nullptr)
		return;
	processor->setProcessing(false);
}
