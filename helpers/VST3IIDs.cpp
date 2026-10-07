/*
    This file is part of Equalizer APO, a system-wide equalizer.

    VST interface ID definitions. The IIDs are declared (DECLARE_CLASS_IID)
    in the vendored Steinberg pluginterfaces headers; they are defined once
    here. Base IIDs (FUnknown, IPluginFactory, IBStream, ...) live in
    thirdparty/vst3 coreiids.cpp.
*/

#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivsthostapplication.h"

namespace Steinberg
{
namespace Vst
{
DEF_CLASS_IID(IComponent)
DEF_CLASS_IID(IAudioProcessor)
DEF_CLASS_IID(IEditController)
DEF_CLASS_IID(IComponentHandler)
DEF_CLASS_IID(IHostApplication)
}
}
