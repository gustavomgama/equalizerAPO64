/*
    This file is part of Equalizer APO, a system-wide equalizer.

    Linux-only helper: choose the PipeWire output node eqapo-host forwards to.

    The host must never forward into its own virtual sink. WirePlumber links a
    target-less stream to the default sink, so when the user makes the virtual
    sink the default (the documented setup), the host's own output loops back
    into itself. Pinning the output to a real sink prevents that.
*/

#pragma once

#include <string>
#include <vector>

namespace eqapo
{
// explicitTarget: --target value ("" = none)
// self:           the host's virtual sink node name
// defaultSink:    WirePlumber's default.audio.sink name ("" if unknown)
// sinks:          every Audio/Sink node.name currently visible
// Returns the node name to target, or "" to leave routing to the default.
inline std::string chooseOutputTarget(const std::string& explicitTarget,
	const std::string& self, const std::string& defaultSink,
	const std::vector<std::string>& sinks)
{
	if (!explicitTarget.empty())
		return explicitTarget;
	if (!defaultSink.empty() && defaultSink != self)
		return defaultSink;
	for (const std::string& sink : sinks)
		if (sink != self)
			return sink;
	return {};
}
}
