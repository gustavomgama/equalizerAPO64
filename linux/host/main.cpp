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
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "FilterEngine.h"
#include "version.h"

namespace {

constexpr unsigned kMaxFrames = 8192;

// Lock-free single-producer/single-consumer ring of interleaved float samples.
// Bounded: on overflow the producer drops the OLDEST unread samples (so latency
// stays bounded and the RT thread never blocks) and increments `overruns`.
struct Ring
{
	std::vector<float> data;
	std::atomic<size_t> writeIdx{0};
	std::atomic<size_t> readIdx{0};
	std::atomic<uint64_t> overruns{0};
	std::atomic<uint64_t> underruns{0};

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
		size_t r = readIdx.load(std::memory_order_acquire);
		// Keep one slot free so full and empty are distinguishable.
		size_t used = w >= r ? w - r : data.size() - r + w;
		size_t freeSamples = data.size() - used - 1;
		if (count > freeSamples)
		{
			// Drop oldest unread samples to make room; bounded, never blocks.
			size_t drop = count - freeSamples;
			r = (r + drop) % data.size();
			readIdx.store(r, std::memory_order_release);
			overruns.fetch_add(1, std::memory_order_relaxed);
		}
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
			underruns.fetch_add(1, std::memory_order_relaxed);
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
	// Separate scratch for the output callback: the two stream process
	// callbacks may run on different threads, so they must not share buffers.
	std::vector<float> popBuf;
	unsigned channels;
	struct pw_main_loop* loop = nullptr;
	struct pw_stream* inStream = nullptr;
	struct pw_stream* outStream = nullptr;
	std::atomic<bool> failed{false};

	explicit Host(unsigned ch)
		: ring(1u << 15, ch), inBuf(kMaxFrames * ch, 0.0f), outBuf(kMaxFrames * ch, 0.0f),
		  popBuf(kMaxFrames * ch, 0.0f), channels(ch)
	{
	}
};

struct pw_main_loop* g_loop = nullptr;

void onSignal(int)
{
	if (g_loop)
		pw_main_loop_quit(g_loop);
}

// Fail fast and loudly on a stream error so a supervisor can restart us,
// instead of stalling silently.
void onStreamState(void* userdata, enum pw_stream_state old, enum pw_stream_state state, const char* error)
{
	Host* h = static_cast<Host*>(userdata);
	std::fprintf(stderr, "eqapo-host: stream %s -> %s%s%s\n",
		pw_stream_state_as_string(old), pw_stream_state_as_string(state),
		error ? ": " : "", error ? error : "");
	if (state == PW_STREAM_STATE_ERROR)
	{
		h->failed.store(true);
		if (h->loop)
			pw_main_loop_quit(h->loop);
	}
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
	h->ring.pop(h->popBuf.data(), count);
	std::memcpy(dst, h->popBuf.data(), count * sizeof(float));
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
	std::string target;
	unsigned rate = 48000;
	unsigned channels = 2;

	for (int i = 1; i < argc; ++i)
	{
		if (!std::strcmp(argv[i], "--config") && i + 1 < argc)
			configPath = argv[++i];
		else if (!std::strcmp(argv[i], "--name") && i + 1 < argc)
			nodeName = argv[++i];
		else if (!std::strcmp(argv[i], "--target") && i + 1 < argc)
			target = argv[++i];
		else if (!std::strcmp(argv[i], "--rate") && i + 1 < argc)
			rate = (unsigned)std::atoi(argv[++i]);
		else if (!std::strcmp(argv[i], "--channels") && i + 1 < argc)
			channels = (unsigned)std::atoi(argv[++i]);
		else if (!std::strcmp(argv[i], "--version"))
		{
			std::printf("eqapo-host %d.%d.%d\n", MAJOR, MINOR, REVISION);
			return 0;
		}
		else if (!std::strcmp(argv[i], "--help") || !std::strcmp(argv[i], "-h"))
		{
			std::printf("usage: eqapo-host [--config <file>] [--name <sink>] [--target <node>] [--rate <hz>] [--channels <n>]\n"
			            "  --config    one-shot config file (default: ~/.config/equalizerapo/config.txt, hot-reloaded)\n"
			            "  --name      virtual sink node name (default: EqualizerAPO)\n"
			            "  --target    output node to forward to (default: system default sink)\n"
			            "  --rate      sample rate (default: 48000)\n"
			            "  --channels  channel count (default: 2)\n"
			            "  --version   print version and exit\n");
			return 0;
		}
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
	// Give the engine a device identity so `Device:` blocks can match on Linux
	// (use `Device: all` or `Device: <sink name>`; Windows device names will
	// not match).
	std::wstring wName(nodeName.begin(), nodeName.end());
	host.engine.setDeviceInfo(false, true, wName, L"", L"", wName);
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
	if (!target.empty())
		pw_properties_set(outProps, PW_KEY_NODE_TARGET, target.c_str());
	struct pw_stream_events outEvents = {};
	outEvents.version = PW_VERSION_STREAM_EVENTS;
	outEvents.process = onOutProcess;
	outEvents.state_changed = onStreamState;
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
	inEvents.state_changed = onStreamState;
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

	// Observability: report over/underruns from a non-RT thread.
	std::atomic<bool> statsStop{false};
	std::thread statsThread([&host, &statsStop]() {
		uint64_t lastOver = 0, lastUnder = 0;
		while (!statsStop.load())
		{
			std::this_thread::sleep_for(std::chrono::seconds(5));
			uint64_t over = host.ring.overruns.load();
			uint64_t under = host.ring.underruns.load();
			if (over != lastOver || under != lastUnder)
			{
				std::fprintf(stderr, "eqapo-host: stats overruns=%llu underruns=%llu\n",
					(unsigned long long)over, (unsigned long long)under);
				lastOver = over;
				lastUnder = under;
			}
		}
	});

	pw_main_loop_run(host.loop);

	statsStop.store(true);
	statsThread.join();

	std::fprintf(stderr, "eqapo-host: stopped (overruns=%llu underruns=%llu)\n",
		(unsigned long long)host.ring.overruns.load(),
		(unsigned long long)host.ring.underruns.load());

	pw_stream_destroy(host.inStream);
	pw_stream_destroy(host.outStream);
	pw_context_destroy(context);
	pw_main_loop_destroy(host.loop);
	pw_deinit();
	return host.failed.load() ? 1 : 0;
}
