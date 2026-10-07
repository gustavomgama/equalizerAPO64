/*
    This file is part of Equalizer APO, a system-wide equalizer.

    Unit tests for the eqapo-host output-target choice. Regression guard for
    the feedback-loop bug: when the default output sink is the host's own
    virtual sink, the host must not target itself.
*/

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "filters/DeviceFilterFactory.h"
#include "linux/host/target.h"

int main()
{
	using eqapo::chooseOutputTarget;
	using eqapo::deviceStringFor;

	// An explicit --target always wins.
	assert(chooseOutputTarget("hw", "self", "self", {"self", "hw"}) == "hw");

	// The default sink is used when it is not us.
	assert(chooseOutputTarget("", "self", "hw", {"self", "hw"}) == "hw");

	// The default is us: pick another real sink (the bug this guards).
	assert(chooseOutputTarget("", "EqualizerAPO", "EqualizerAPO",
		{"EqualizerAPO", "alsa_output.hw"}) == "alsa_output.hw");

	// Only our own sink exists: leave routing to the default.
	assert(chooseOutputTarget("", "self", "self", {"self"}).empty());

	// No default: fall back to the first non-self sink.
	assert(chooseOutputTarget("", "self", "", {"self", "hw"}) == "hw");

	// Dummy sinks are never chosen, even when default or first listed.
	assert(eqapo::isDummySink("auto_null"));
	assert(eqapo::isDummySink("Dummy-Driver"));
	assert(!eqapo::isDummySink("alsa_output.hw"));
	assert(chooseOutputTarget("", "self", "auto_null", {"self", "auto_null", "hw"}) == "hw");
	assert(chooseOutputTarget("", "self", "", {"self", "auto_null"}).empty());

	// The Editor writes `Device: <description> <name> <name>` for the sink the
	// user picked; the host's identity must match it (the bug where only the
	// EqualizerAPO entry worked).
	const std::wstring desc = L"Starship/Matisse HD Audio Controller Analog Stereo";
	const std::wstring name = L"alsa_output.pci-0000_08_00.4.analog-stereo";
	const std::wstring self = L"EqualizerAPO";
	const std::wstring hostString = deviceStringFor(desc, name, self);
	assert(DeviceFilterFactory::matchDevice(hostString, desc + L" " + name + L" " + name));
	assert(DeviceFilterFactory::matchDevice(hostString, L"all"));
	assert(DeviceFilterFactory::matchDevice(hostString, L"EqualizerAPO"));
	assert(!DeviceFilterFactory::matchDevice(hostString, L"Speakers"));

	std::printf("test_target: all checks passed\n");
	return 0;
}
