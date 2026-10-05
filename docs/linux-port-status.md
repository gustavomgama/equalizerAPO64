# EqualizerAPO — Linux Port Status

Status of the native Linux port. The Windows build is unchanged and still
ships; Linux support is additive behind `#ifdef _WIN32` guards, enforced by the
`windows_untouched` test.

## Implemented

| Component | Status |
|---|---|
| Filter engine (parser, `FilterEngine`, `FilterConfiguration`) | Native Linux build |
| Filter set | BiQuad, IIR, GraphicEQ, Convolution, Delay, Copy, Channel, Include, Device, Expression, If, Stage, Preamp, **LoudnessCorrection** |
| `eqapo-null` | WAV in → engine → WAV out (DSP verification) |
| `eqapo-host` | Native PipeWire virtual sink, hot-reload, fail-fast, stats |
| VST2 | Native `.so` via `dlopen`; relative paths under `~/.vst` |
| `eqapo-editor` | Qt6 GUI: edits `config.txt`, live frequency response, PipeWire device list, all filter GUIs incl. loudness |
| Packaging | `cmake --install`, desktop entry, systemd user unit |

## Build and install

    # deps (Arch): sudo pacman -S cmake fftw libsndfile pipewire qt6-base
    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build -j2                 # -j2 keeps CPU/memory in check
    ctest --test-dir build --output-on-failure
    cmake --install build --prefix ~/.local

muparserx is fetched and built automatically if not installed.

## Usage

    eqapo-editor            # configure the filter chain
    eqapo-host              # system-wide EQ via the "EqualizerAPO" virtual sink
    eqapo-null --in a.wav --out b.wav --config config.txt

Route applications to the `EqualizerAPO` sink, or set it as the default sink,
to apply the chain system-wide. The host hot-reloads
`~/.config/equalizerapo/config.txt`.

## Known limitations on Linux

1. **Loudness correction** reads the default sink volume via `wpctl`
   (WirePlumber). It works when `wpctl` is present; the value is cached for
   500 ms to avoid spawning a process on every poll. A native libpipewire
   reader is a possible follow-up.
2. **`Device:` blocks** match against the Linux sink name (`EqualizerAPO`).
   Use `Device: all` or `Device: EqualizerAPO`; a Windows device name such as
   `Device: Speakers` will not match and the block is skipped (the engine logs
   this at trace level). This is the main config-portability gotcha.
3. **APO install / registration checks** and the **Device Selector wizard** are
   Windows-only and are no-ops on Linux with a log message.
3. **VST crash isolation**: a segfaulting native plugin takes down `eqapo-host`
   (systemd restarts it). Windows used SEH; Linux has no in-process equivalent.
   Out-of-process hosting is the follow-up. Treat third-party plugins as
   trusted code for now.
4. **FabFilter and other Windows VSTs** are PE binaries and cannot be
   `dlopen`ed; use yabridge to expose them as native VST2 `.so`.
5. **Multi-device `Device:` routing** currently maps to a single virtual sink;
   per-device sinks are a follow-up.

## Verification

`ctest` runs 12 tests: `platform`, `windows_untouched`, `strings`,
`engine_preamp`, `filters`, `null_e2e` (real WAV through the engine),
`host_e2e` (live PipeWire virtual sink + link), `vst_e2e` (stub VST2 plugin
loaded via `dlopen` and its gain asserted), `full_chain_e2e` (Preamp,
GraphicEQ, BiQuad, Delay, Copy, LoudnessCorrection, Convolution, and a VST2
plugin in one config), `device_e2e` (`Device:` routing semantics),
`logging_e2e` (wide-string log formatting), and `dsp_e2e` (low-pass gain at
100 Hz vs 10 kHz — validates the filter math, not just finiteness).

The `full_chain_e2e` config exercises **every filter type**: Preamp, Eval,
If/Else/EndIf, GraphicEQ, BiQuad (PK/HP), IIR, Copy, Channel, Delay, Stage,
Convolution, LoudnessCorrection, and VST2.
