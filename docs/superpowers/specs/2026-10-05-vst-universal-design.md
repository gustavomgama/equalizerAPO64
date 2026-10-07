# Universal VST Support — Design Spec

**Date:** 2026-10-05
**Status:** Approved (user) — implement Phase 1 → Phase 2 → Phase 3.
**Scope:** Make VST2/VST3 plugins of any origin work in the Linux host: native
Linux VST2/VST3, and Windows VST2 (`.dll`) / VST3 (`.vst3`) through yabridge
(Wine). Windows PE binaries cannot be loaded by Linux code, so a Wine bridge is
mandatory; yabridge is the standard and is reused, not reimplemented.

## 1. Goal

`VSTPlugin: Library <path>` works for `.so` (VST2/VST3), `.dll` (Windows VST2),
and `.vst3` (VST3, Linux or Windows). Windows paths resolve to the yabridge
wrapper when one exists; otherwise the log tells the user the exact
`yabridgectl add` command. Native VST3 is hosted in-engine.

## 2. Non-goals

- No in-tree Wine bridge (reimplementing yabridge). yabridge is a dependency.
- No VST3 plugin GUI windows in the Editor in this effort; VST3 parameters are
  readable/editable, the native window is not embedded (audio correctness first).
- No 32-bit plugin support beyond what yabridge provides. x86_64 only.
- No changes to the Windows build or the `VSTPlugin:` config syntax.
- No AU/LV2/CLAP formats.

## 3. Decisions (locked)

| Decision | Choice |
|---|---|
| Windows hosting | yabridge (Wine), NOT in-tree. `wine` + `yabridgectl` + AUR `yabridge`/`yabridge-bin` are `optdepends`; docs carry setup. |
| VST3 headers | CMake `FetchContent` of `steinbergmedia/vst3sdk` at a pinned tag; use only `pluginterfaces` headers (no SDK sources). GPLv3 — compatible with this repo's `GPL-2.0-or-later`; noted in docs. |
| VST3 plugin shape | Hand-rolled native VST3 host (`VST3PluginInstance`/`VST3PluginLibrary`) behind the same abstract instance/library the existing filter uses; float32 processing with the existing double↔float plumbing; state serialized as base64 into `ChunkData` so VST2/VST3 share the config schema; params by name into `paramMap`. |
| Format detection | By content capability, not extension: probe `GetPluginFactory` (VST3) else `VSTPluginMain`/`main` (VST2). Windows PE (MZ header) is never `dlopen`ed directly; it maps to the yabridge wrapper. |
| Wrapper layout | yabridge defaults: `~/.vst/yabridge/<Basename>.so` for VST2 `.dll`, `~/.vst3/yabridge/<Basename>.vst3` (bundle) for VST3 `.vst3`. Direct references to the wrapper always work. |
| Editor | Reuse the existing VST parameter dialog behind a format-agnostic param adapter; no native VST3 window embedding. |
| Tests | Mirror the existing VST2 stub: a hand-rolled minimal VST3 gain stub (factory + component/processor/controller, no SDK sources) loaded through the real host path (`null_e2e`-style). Unit tests for format detection/resolution. The existing `vst_e2e` must stay green. |

## 4. Architecture

### 4.1 Loader (`VSTPluginFilterFactory` + new `PluginFormat`)

```
Library <path>
  ├─ readable PE (MZ) ──► resolve yabridge wrapper ──► native .so/.vst3
  ├─ .vst3 bundle dir  ──► Contents/<arch>/<name>.so
  └─ native .so         ──► probe GetPluginFactory → VST3, else VST2
```

Resolution never `dlopen`s a PE. A missing wrapper logs the exact
`yabridgectl add "<dir>"` invocation and fails the filter to passthrough
(the existing `skipProcessing` behavior).

### 4.2 VST2 (existing)

Unchanged behavior. `VSTPluginInstance`/`VSTPluginLibrary` gain abstract bases
so the filter can hold either format; method-for-method compatible, so the
31-test suite still passes unchanged.

### 4.3 VST3 (new)

`VST3PluginLibrary`: opens the bundle `.so`, `dlsym`s `GetPluginFactory`,
enumerates classes, instantiates the first `kVstAudioEffectClass` component.

`VST3PluginInstance`: implements the same abstract instance the filter holds:
- Host side: `IHostApplication` (name only), `IComponentHandler` (no-op),
  memory `IAttributeList`/`IBStream`.
- Lifecycle: `initialize` → `setActive(true)` → `setupProcessing`
  (48 kHz stereos, `kSample32`, `kRealtime`) → `setProcessing(true)`.
- `process`: map our float channel buffers into `ProcessData`/`AudioBusBuffers`,
  set `kSample32`; call `process`; read back (in-place supported).
- State: `IComponent::getState` → base64 → `ChunkData`; `setState` on
  `writeToEffect`. Params: `IEditController::setParamNormalized` from `paramMap`
  keyed by `ParamInfo.title`; `getParamNormalized` into `paramMap` on read.
- Latency: `processor->getLatencySamples()` drives the existing delay buffer
  (`getInitialDelay`).

### 4.4 yabridge (dependency + resolution)

- `wine` (`extra`) and `yabridge`/`yabridge-bin` (AUR) become package
  `optdepends` with a `yabridge` setup script (`packaging/arch/yabridge-setup.sh`)
  that verifies `wine`/`yabridgectl`, adds `~/.vst*/…` dirs, and re-syncs.
- Resolution and actionable logging above. No network at build; no Wine at build.

### 4.5 Editor

The VST parameter dialog reads parameters by name/value through an adapter that
speaks VST2 (`get_parameter`) or VST3 (`getParamNormalized`) and writes them
back into the same `VSTPlugin:` line schema (`ChunkData` + named params). No
`IPlugView` embedding.

## 5. Phases

1. **Loader + yabridge resolution + setup/docs.** Windows VST2 `.dll` works
   end-to-end via the wrapper (no new subsystem). Unit tests for format/PE
   detection and wrapper mapping. Ship in the Arch package + README/docs.
2. **VST3 subsystem** (§4.3) + hand-rolled VST3 gain stub + `vst3_e2e` test
   (load through `eqapo-null`, assert gain/attenuation and bypass passthrough).
   Unlocks Linux `.vst3` and Windows `.vst3` (via yabridge).
3. **Editor VST3 params** (§4.5). Manual verification with a Linux VST3 (or the
   stub) in `eqapo-editor`.

## 6. Risks

| Risk | Mitigation |
|---|---|
| VST3 SDK pin rots / network build | Pinned tag + `FETCHCONTENT_SOURCE_DIR_VST3SDK` from PKGBUILD `source=()`, like muparserx. |
| `GetPluginFactory` ABI drift across VST3 SDKs | Only the stable `pluginterfaces` ABI is used; version is checked via `IPluginFactory::getFactoryInfo`. |
| yabridge wrapper layout changes | Resolution is a lookup with a clear fallback message, not a hard assumption; direct wrapper refs always work. |
| Wine/AUR unavailable on user box | The feature degrades to a precise log message; native VST2/VST3 never need Wine. |
| Touching the tested VST2 path | The refactor is interface-only (same call sequence); `vst_e2e` + full suite run after every change. |

## 7. Open questions (resolved during implementation)

- Exact yabridge wrapper naming for edge cases (spaces, 32-bit) — verify with
  `yabridgectl` during Phase 1; fall back to documented direct refs.
- Whether the fixed `vst3sdk` tag's headers compile standalone — verified at
  Phase 2 start; otherwise vendor the minimal `pluginterfaces` copy.
