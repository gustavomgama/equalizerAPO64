# Vendored VST3 interface headers

`pluginterfaces/` is a minimal copy of Steinberg's `vst3_pluginterfaces`
(v3.7.9, commit `f0eeef7`) plus `coreiids.cpp`/`funknown.cpp`, which define the
standard interface IDs. Only headers and these two translation units are used;
no SDK sources are built. Hosting (`helpers/VST3*`) and the test stub are ours.

Source: https://github.com/steinbergmedia/vst3sdk (tag `v3.7.9_build_61`,
submodules `vst3_pluginterfaces` + `vst3_base` at the tag's pins).

License: GPLv3 (see `LICENSE`). Compatible with this repository's
`GPL-2.0-or-later`. Our VST3 host code links these interfaces, so VST3-enabled
builds are effectively GPLv3-or-later for the VST3 parts.
