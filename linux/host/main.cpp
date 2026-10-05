/*
    This file is part of Equalizer APO, a system-wide equalizer.

    Linux-only PipeWire host: exposes a virtual sink, runs the engine on its
    audio, and forwards the result to the default output. Not part of the
    Windows build.
*/

#include <pipewire/pipewire.h>
#include <spa/param/audio/raw.h>
#include <spa/param/audio/raw-utils.h>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "FilterEngine.h"

namespace {

constexpr unsigned kMaxFrames = 8192;

// Lock-free single-producer/single-consumer ring of interleaved float samples.
struct Ring
{
	std::vector<float> data;
	std::atomic<size_t> writeIdx{0};
	std::atomic<size_t> readIdx{0};

	Ring(size_t frames, unsigned channels) : data(frames * channels, 0.0f) {}

	size_t available() const
	{
		size_t w = writeIdx.load(std::memory_order_acquire);
		size_t r = readIdx.load(std::memory_order_acquire);
		return w >= r ? w - r : data.size() - r + w;
	}

	void push(const float* src, size_t count)
	{
		size_t w = writeIdx.load(std::memory_order_relaxed);
		for (size_t i = 0; i < count; ++i)
		{
			data[w] = src[i];
			w = (w + 1 == data.size()) ? 0 : w + 1;
		}
		writeIdx.store(w, std::memory_order_release);
	}

	void pop(float* dst, size_t count)
	{
		size_t r = readIdx.load(std::memory_order_relaxed);
		if (available() < count)
		{
			std::memset(dst, 0, count * sizeof(float));
			return;
		}
		for (size_t i = 0; i < count; ++i)
		{
			dst[i] = data[r];
			r = (r + 1 == data.size()) ? 0 : r + 1;
		}
		readIdx.store(r, std::memory_order_release);
	}
};

struct Host
{
	FilterEngine engine;
	Ring ring;
	std::vector<float> inBuf;
	std::vector<float> outBuf;
	unsigned channels;
	struct pw_main_loop* loop = nullptr;
	struct pw_stream* inStream = nullptr;
	struct pw_stream* outStream = nullptr;

	explicit Host(unsigned ch)
		: ring(1u << 15, ch), inBuf(kMaxFrames * ch, 0.0f), outBuf(kMaxFrames * ch, 0.0f), channels(ch)
	{
	}
};

struct pw_main_loop* g_loop = nullptr;

void onSignal(int)
{
	if (g_loop)
		pw_main_loop_quit(g_loop);
}

void onInProcess(void* userdata)
{
	Host* h = static_cast<Host*>(userdata);
	struct pw_buffer* b = pw_stream_dequeue_buffer(h->inStream);
	if (!b)
		return;

	struct spa_buffer* buf = b->buffer;
	float* src = static_cast<float*>(buf->datas[0].data);
	uint32_t size = buf->datas[0].chunk ? buf->datas[0].chunk->size : 0;
	if (src && size > 0)
	{
		unsigned frames = size / (h->channels * sizeof(float));
		size_t offset = 0;
		while (frames > 0)
		{
			unsigned chunk = frames > kMaxFrames ? kMaxFrames : frames;
			size_t count = chunk * h->channels;
			std::memcpy(h->inBuf.data(), src + offset, count * sizeof(float));
			h->engine.process(h->outBuf.data(), h->inBuf.data(), chunk);
			h->ring.push(h->outBuf.data(), count);
			offset += count;
			frames -= chunk;
		}
	}
	pw_stream_queue_buffer(h->inStream, b);
}

void onOutProcess(void* userdata)
{
	Host* h = static_cast<Host*>(userdata);
	struct pw_buffer* b = pw_stream_dequeue_buffer(h->outStream);
	if (!b)
		return;

	struct spa_buffer* buf = b->buffer;
	float* dst = static_cast<float*>(buf->datas[0].data);
	if (!dst)
	{
		pw_stream_queue_buffer(h->outStream, b);
		return;
	}

	unsigned frames = buf->datas[0].maxsize / (h->channels * sizeof(float));
	if (frames > kMaxFrames)
		frames = kMaxFrames;
	size_t count = frames * h->channels;
	h->ring.pop(h->outBuf.data(), count);
	std::memcpy(dst, h->outBuf.data(), count * sizeof(float));
	if (buf->datas[0].chunk)
	{
		buf->datas[0].chunk->offset = 0;
		buf->datas[0].chunk->stride = h->channels * sizeof(float);
		buf->datas[0].chunk->size = count * sizeof(float);
	}
	pw_stream_queue_buffer(h->outStream, b);
}

const struct spa_pod* makeFormat(struct spa_pod_builder* b, unsigned channels, unsigned rate)
{
	struct spa_audio_info_raw info = {};
	info.format = SPA_AUDIO_FORMAT_F32;
	info.rate = rate;
	info.channels = channels;
	for (unsigned i = 0; i < channels && i < SPA_AUDIO_MAX_CHANNELS; ++i)
	{
		if (i == 0)
			info.position[i] = SPA_AUDIO_CHANNEL_FL;
		else if (i == 1)
			info.position[i] = SPA_AUDIO_CHANNEL_FR;
		else
			info.position[i] = SPA_AUDIO_CHANNEL_UNKNOWN;
	}
	return spa_format_audio_raw_build(b, SPA_PARAM_EnumFormat, &info);
}

} // namespace

int main(int argc, char** argv)
{
	std::string configPath;
	std::string nodeName = "EqualizerAPO";
	unsigned rate = 48000;
	unsigned channels = 2;

	for (int i = 1; i < argc; ++i)
	{
		if (!std::strcmp(argv[i], "--config") && i + 1 < argc)
			configPath = argv[++i];
		else if (!std::strcmp(argv[i], "--name") && i + 1 < argc)
			nodeName = argv[++i];
		else if (!std::strcmp(argv[i], "--rate") && i + 1 < argc)
			rate = (unsigned)std::atoi(argv[++i]);
		else if (!std::strcmp(argv[i], "--channels") && i + 1 < argc)
			channels = (unsigned)std::atoi(argv[++i]);
	}
	if (channels < 1)
		channels = 1;
	if (channels > 8)
		channels = 8;

	pw_init(&argc, &argv);

	Host host(channels);

	// Empty custom path => engine uses the default config dir and starts its
	// inotify watcher, giving hot-reload. An explicit --config is one-shot.
	std::wstring cfg;
	if (!configPath.empty())
		cfg.assign(configPath.begin(), configPath.end());
	host.engine.initialize((float)rate, channels, channels, channels, 0, kMaxFrames, cfg);

	host.loop = pw_main_loop_new(nullptr);
	if (!host.loop)
	{
		std::fprintf(stderr, "eqapo-host: cannot create main loop\n");
		return 1;
	}
	g_loop = host.loop;
	std::signal(SIGINT, onSignal);
	std::signal(SIGTERM, onSignal);

	struct pw_context* context = pw_context_new(pw_main_loop_get_loop(host.loop), nullptr, 0);
	if (!context)
	{
		std::fprintf(stderr, "eqapo-host: cannot create context\n");
		return 1;
	}
	struct pw_core* core = pw_context_connect(context, nullptr, 0);
	if (!core)
	{
		std::fprintf(stderr, "eqapo-host: cannot connect to PipeWire\n");
		return 1;
	}

	// Output stream: auto-connect to the default sink.
	struct pw_properties* outProps = pw_properties_new(
		PW_KEY_MEDIA_TYPE, "Audio",
		PW_KEY_MEDIA_CATEGORY, "Playback",
		PW_KEY_MEDIA_CLASS, "Stream/Output/Audio",
		PW_KEY_NODE_NAME, "eqapo_output",
		PW_KEY_APP_NAME, "EqualizerAPO",
		nullptr);
	struct pw_stream_events outEvents = {};
	outEvents.version = PW_VERSION_STREAM_EVENTS;
	outEvents.process = onOutProcess;
	host.outStream = pw_stream_new_simple(pw_main_loop_get_loop(host.loop), "eqapo-output",
		outProps, &outEvents, &host);

	uint8_t outPodBuf[1024];
	struct spa_pod_builder outBuilder = SPA_POD_BUILDER_INIT(outPodBuf, sizeof(outPodBuf));
	const struct spa_pod* outParams[1];
	outParams[0] = makeFormat(&outBuilder, channels, rate);
	pw_stream_connect(host.outStream, PW_DIRECTION_OUTPUT, PW_ID_ANY,
		(pw_stream_flags)(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS),
		outParams, 1);

	// Input stream: a virtual sink applications can route to.
	struct pw_properties* inProps = pw_properties_new(
		PW_KEY_MEDIA_TYPE, "Audio",
		PW_KEY_MEDIA_CATEGORY, "Capture",
		PW_KEY_MEDIA_CLASS, "Audio/Sink",
		PW_KEY_NODE_NAME, nodeName.c_str(),
		PW_KEY_NODE_DESCRIPTION, "EqualizerAPO",
		PW_KEY_APP_NAME, "EqualizerAPO",
		nullptr);
	struct pw_stream_events inEvents = {};
	inEvents.version = PW_VERSION_STREAM_EVENTS;
	inEvents.process = onInProcess;
	host.inStream = pw_stream_new_simple(pw_main_loop_get_loop(host.loop), "eqapo-input",
		inProps, &inEvents, &host);

	uint8_t inPodBuf[1024];
	struct spa_pod_builder inBuilder = SPA_POD_BUILDER_INIT(inPodBuf, sizeof(inPodBuf));
	const struct spa_pod* inParams[1];
	inParams[0] = makeFormat(&inBuilder, channels, rate);
	pw_stream_connect(host.inStream, PW_DIRECTION_INPUT, PW_ID_ANY,
		(pw_stream_flags)(PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS),
		inParams, 1);

	std::fprintf(stderr, "eqapo-host: running (sink '%s', %u Hz, %u ch)\n",
		nodeName.c_str(), rate, channels);

	pw_main_loop_run(host.loop);

	pw_stream_destroy(host.inStream);
	pw_stream_destroy(host.outStream);
	pw_context_destroy(context);
	pw_main_loop_destroy(host.loop);
	pw_deinit();
	return 0;
}
