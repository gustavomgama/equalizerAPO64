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

#include "linux/host/target.h"

int main()
{
	using eqapo::chooseOutputTarget;

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

	std::printf("test_target: all checks passed\n");
	return 0;
}
