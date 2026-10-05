# EqualizerAPO Linux Port — Design Spec

**Date:** 2026-10-05
**Status:** Implemented — see `docs/linux-port-status.md` for current state and
known limitations. Windows build preserved.
**Scope:** Native Linux system-wide equalizer built from this repository's engine,
config format, and Qt Editor.

## 1. Goal

Run EqualizerAPO's filter engine natively on Linux as a system-wide audio
processor, preserving the original's `config.txt` format, filter set, and
Editor experience as closely as practical. VST2 plugin support is a first-class
requirement.

## 2. Non-goals

- No Wine for the application or host. The Linux build is native.
- No VST3 hosting in the first effort (native VST2 only; VST3 is a later
  sub-project if wanted).
- **No removal of Windows support.** The Windows build (VS solution,
  `.vcxproj`, `build.bat`, `Editor.pro`, installer, registry, APO
  infrastructure) is preserved unchanged. Linux is added *alongside* behind
  `#ifdef _WIN32` guards. The project must continue to produce both a Windows
  and a Linux version.
- No config-format changes. Existing `config.txt` files parse unchanged.

## 3. Decisions (locked)

| Decision | Choice |
|---|---|
| Host model | Native PipeWire client daemon (EasyEffects-style) |
| Config exchange | File-based (Editor edits file, host watches with inotify) — matches original |
| Build system | CMake, C++17 |
| VST | Native VST2 `.so` via `dlopen` (keep in-repo `aeffectx.h`) |
| Test corpus | In-repo stub VST2 plugin (no external deps); yabridge/FabFilter optional, later |
| Source layout | Keep existing dirs; add `linux/` and `CMakeLists.txt` |
| Windows support | Preserved and untouched; Linux added behind `#ifdef _WIN32` guards |
| Build (Windows) | Existing VS solution / `build.bat` / `Editor.pro` unchanged |
| Internal strings | Keep `std::wstring`; portable UTF-8 ↔ UTF-32 at boundaries |

## 4. Architecture

Two native processes, mirroring the original Windows split (engine host vs.
editor):

```
 apps ──► [ EqualizerAPO virtual sink ] ──► default output device
                    │
            eqapo-host (PipeWire client)
              - FilterEngine::process() in RT callback
              - watches ~/.config/equalizerapo/config.txt (inotify)

 eqapo-editor (Qt6)  ── edits config.txt ──► host picks it up
                     └─ captures sink monitor for live spectrum
```

No IPC protocol is required for phase 1: the Editor and host communicate
through the config file, exactly as the Windows Editor and APO do.

### 4.1 Components

- **`eqapo_core`** (static lib): `parser/`, `IFilter`, `FilterConfiguration`,
  `FilterEngine`, portable `filters/`.
- **`eqapo-host`** (executable): `linux/host/` — PipeWire stream management,
  virtual sink, config watching, RT-safe engine invocation.
- **`eqapo-vst-test`** (shared lib): `linux/vst-test/` — minimal VST2 gain
  plugin built from `helpers/aeffectx.h`; proves the host.
- **`eqapo-editor`** (Qt6 executable): ported `Editor/`.

### 4.2 Data flow

1. `eqapo-host` creates a virtual sink node and a stream linked to the default
   output.
2. Apps route to the virtual sink.
3. Capture callback: interleaved float frames → deinterleave → engine
   `process()` (double precision) → interleave → output stream.
4. On `config.txt` change (inotify), the engine reloads on its existing worker
   thread and cross-fades (existing `doTransition` logic).

### 4.3 Dependencies

`libpipewire-0.3` 1.6.x, `fftw3` 3.3.x, `sndfile` 1.2.x, `muparserx` 4.0.12
(AUR), Qt6 6.x, `inotify` (glibc). No new third-party dependency is introduced
beyond what the project already uses, except the build system change.

## 5. Cross-platform layer (Windows preserved)

All work here is **additive**. Each shared file keeps its Windows code path
under `#ifdef _WIN32` byte-for-byte, and the Linux implementation is added
under `#else`. No Windows-only file is edited. A guard check (see §9) fails
the build if a Windows-only file is modified or a Windows branch disappears.

### 5.1 String layer

Keep `std::wstring` internally to minimize the diff. Add portable UTF-8 ↔
UTF-32 conversions under `#else`; the Windows `MultiByteToWideChar` path stays
under `#ifdef _WIN32`. Config files are UTF-8 on disk. (Note: Linux `wchar_t`
is 32-bit; code must not assume UTF-16.)

### 5.2 Platform glue

Windows implementations stay; Linux equivalents are added under `#else`:

| Windows (kept, `#ifdef _WIN32`) | Linux (added, `#else`) |
|---|---|
| `MemoryHelper` `_aligned_malloc` | `posix_memalign` |
| `PrecisionTimer` (`QueryPerformanceCounter`) | `std::chrono::steady_clock` |
| `LogHelper` (`OutputDebugString`, `GetTempPathW`) | file log under `$XDG_STATE_HOME` |
| `RegistryHelper` (kept for Windows) | `ConfigPathHelper` (Linux only) |
| `RegistryFunctions` (`reg()`) | stub returning empty (Linux only) |
| `ServiceHelper`, `TaskSchedulerHelper` | Linux host owns lifecycle |

### 5.3 FilterEngine

Windows path preserved under `#ifdef _WIN32`; Linux path added under `#else`:

- Keep `windows.h`/`Shlwapi.h`/`Ks*.h` includes for Windows; exclude on Linux.
- `CreateSemaphore` / `CRITICAL_SECTION` (Windows) vs `std::mutex` +
  `std::condition_variable` (Linux).
- Notification thread: `CreateThread` + `FindFirstChangeNotificationW`
  (Windows) vs `std::thread` + inotify (Linux).
- Config path: `RegistryHelper::readValue` (Windows) vs
  `ConfigPathHelper::getConfigDir` (Linux).
- File read: `CreateFile`/`ReadFile` (Windows) vs `std::ifstream` (Linux).

### 5.4 Filters

Each filter keeps its Windows branch; Linux branch added under `#else`:

- **Convolution / GraphicEQ**: `ENABLE_SNDFILE_WINDOWS_PROTOTYPES` stays for
  Windows; on Linux use UTF-8 paths with libsndfile.
- **Include**: `PathIsRelativeW` (Windows) vs `std::filesystem` (Linux).
- **VolumeController** (loudness correction): `IAudioEndpointVolume` (Windows)
  vs PipeWire-sourced value or stub (Linux).
- **Device**: Windows device strings vs PipeWire node name/description (Linux).
- **VST**: `LoadLibrary` (Windows) vs `dlopen` (Linux); `.dll` vs `.so`; entry
  point `VSTPluginMain` (fallback `main`); wide vs UTF-8 paths.

## 6. Linux host (PipeWire)

- Native `pw_stream` client (not a module; full control, matches original
  per-device insertion semantics via a virtual sink).
- RT callback constraints: no allocation, no locking. The engine's existing
  worker-thread config transition is reused; buffer resizing happens before
  the stream starts or via pre-allocated pools.
- `Device:` filter targets map to PipeWire node names.
- CLI flags for: config path, sink name, sample rate, and a `--null` mode
  (below).

## 7. VST2 native

- Port the existing VST2 path to `dlopen` with the in-repo `aeffectx.h`.
- Build `linux/vst-test/` stub gain plugin; load through the host.
- yabridge + FabFilter Windows DLLs are an optional integration test only.
  (The provided corpus is PE32+ Windows DLLs and cannot be `dlopen`ed
  directly.)

## 8. Editor

- Port build to CMake + Qt6 (already Qt6-compatible sources).
- `RegistryHelper` usage → config-path helper.
- Device enumeration (WASAPI/`DeviceSelector`) → PipeWire enumeration.
- Live spectrum: capture the virtual sink monitor (replaces WASAPI loopback in
  `AnalysisThread`).
- GUI layout and behavior unchanged.

## 9. Testing & verification

1. **`--null` mode**: WAV in (libsndfile) → engine → WAV out. Verifies DSP in
   CI with no audio server.
2. **Golden-sample tests**: known config → assert exact double-precision
   output.
3. **Live test**: `pw-play` a tone into the virtual sink → `pw-record` the
   monitor → assert the filter was applied.
4. **VST test**: stub plugin loaded and processing through the host.
5. **Windows-untouched guard**: a script asserts that no Windows-only file
   (`.vcxproj`, `.sln`, `.bat`, `.pro`, `.rc`) is modified, and that every
   edited shared source file still contains its `#ifdef _WIN32` branch. Fails
   the suite if the Windows port is disturbed.

## 10. Phasing

1. CMake + `eqapo_core` compiles on Linux.
2. `--null` host + golden tests → DSP verified.
3. PipeWire host → audible system-wide EQ.
4. Native VST2 + stub plugin test.
5. Editor port.
6. (Optional) yabridge / FabFilter integration.

## 11. Risks

- **`wchar_t` sweep** — true size unknown until first compile; primary schedule
  risk. Mitigation: compile early, fix iteratively, keep `std::wstring`.
- **PipeWire RT callback** — must not allocate or lock. Mitigation: audit
  `FilterEngine::process` path; pre-allocate buffers.
- **Loudness correction** — system-volume semantics differ from Windows.
  Mitigation: PipeWire-sourced value or stub in phase 1.
- **muparserx availability** — built from the fork via CMake `FetchContent`
  (pinned commit) when no system install is found, so no AUR dependency is
  required.

## 12. Open questions

- Exact PipeWire routing model for multi-device `Device:` targeting (one sink
  per device vs. one sink + links). Resolve in phase 3.
- Whether the Editor needs any live control channel beyond the config file
  (e.g., to display current filter state while the host runs). Resolve in
  phase 5; default is file-only.
