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
// PipeWire placeholder sinks that must never be an output target. At boot the
// real hardware may not be enumerated yet; settling for one of these mutes
// the host instead of waiting for hardware.
inline bool isDummySink(const std::string& name)
{
	return name == "auto_null" || name == "Dummy-Driver" || name == "Freewheel-Driver"
		|| name.rfind("auto_null.", 0) == 0;
}

inline std::string chooseOutputTarget(const std::string& explicitTarget,
	const std::string& self, const std::string& defaultSink,
	const std::vector<std::string>& sinks)
{
	if (!explicitTarget.empty())
		return explicitTarget;
	if (!defaultSink.empty() && defaultSink != self && !isDummySink(defaultSink))
		return defaultSink;
	for (const std::string& sink : sinks)
		if (sink != self && !isDummySink(sink))
			return sink;
	return {};
}

// The engine deviceString for a resolved output sink. The Editor's device
// picker writes a `Device:` pattern of "<description> <name> <name>"; the
// virtual sink name is included too so configs written against either
// identity still match.
inline std::wstring deviceStringFor(const std::wstring& description,
	const std::wstring& name, const std::wstring& self)
{
	return name + L" " + description + L" " + name + L" " + self;
}
}
