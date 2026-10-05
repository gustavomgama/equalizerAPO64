/*
    Minimal native VST2 test plugin for the EqualizerAPO Linux host.

    It implements the in-repo `aeffectx.h` ABI and applies a fixed 0.5 gain, so
    a host test can assert the plugin ran. Linux-only test asset.
*/

#include <cstdint>
#include <cstring>

#include "linux/wincompat.h"
#include "helpers/aeffectx.h"

namespace {

constexpr float kGain = 0.5f;

intptr_t stubControl(vst_effect_t* effect, int32_t opcode, int32_t index, intptr_t value, void* ptr, float opt)
{
	(void)effect;
	(void)index;
	(void)value;
	(void)opt;
	switch (opcode)
	{
	case VST_EFFECT_OPCODE_EFFECT_NAME:
		if (ptr)
		{
			std::strncpy((char*)ptr, "Stub Gain", 63);
			((char*)ptr)[63] = '\0';
		}
		return 1;
	case VST_EFFECT_OPCODE_PARAM_GET_NAME:
		if (ptr)
		{
			std::strncpy((char*)ptr, "Gain", 63);
			((char*)ptr)[63] = '\0';
		}
		return 1;
	default:
		return 0;
	}
}

void stubSetParameter(vst_effect_t*, uint32_t, float) {}
float stubGetParameter(vst_effect_t*, uint32_t) { return kGain; }

void stubProcessFloat(vst_effect_t*, const float* const* inputs, float** outputs, int32_t samples)
{
	for (int32_t c = 0; c < 2; ++c)
		for (int32_t i = 0; i < samples; ++i)
			outputs[c][i] = inputs[c][i] * kGain;
}

void stubProcessDouble(vst_effect_t*, const double* const* inputs, double** outputs, int32_t samples)
{
	for (int32_t c = 0; c < 2; ++c)
		for (int32_t i = 0; i < samples; ++i)
			outputs[c][i] = inputs[c][i] * (double)kGain;
}

} // namespace

extern "C" vst_effect_t* VSTPluginMain(vst_host_callback_t)
{
	static vst_effect_t effect;
	std::memset(&effect, 0, sizeof(effect));
	effect.magic_number = VST_MAGICNUMBER;
	effect.control = stubControl;
	effect.set_parameter = stubSetParameter;
	effect.get_parameter = stubGetParameter;
	effect.num_programs = 0;
	effect.num_params = 1;
	effect.num_inputs = 2;
	effect.num_outputs = 2;
	effect.flags = VST_EFFECT_FLAG_SUPPORTS_FLOAT | VST_EFFECT_FLAG_SUPPORTS_DOUBLE;
	effect.process_float = stubProcessFloat;
	effect.process_double = stubProcessDouble;
	return &effect;
}
