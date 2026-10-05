# EqualizerAPO Linux Native VST2 Host — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Load native Linux VST2 `.so` plugins in the EqualizerAPO engine on Linux, using the in-repo `aeffectx.h` ABI, without changing the Windows build.

**Architecture:** The existing VST host (`AbstractLibrary`, `VSTPluginLibrary`, `VSTPluginInstance`, `VSTPluginFilter`) keeps its Windows implementation under `#ifdef _WIN32` and gains a POSIX branch: `dlopen`/`dlsym` instead of `LoadLibrary`/`GetProcAddress`, portable base64 instead of `wincrypt`, and the SEH `__try/__except` guards removed on Linux. A stub VST2 plugin built from the same header proves the host.

**Tech Stack:** C++17, `dlfcn`, the existing `eqapo_core`.

**Spec:** `docs/superpowers/specs/2026-10-05-equalizerapo-linux-port-design.md` (§7)

## Global Constraints

- Windows branches preserved byte-for-byte; Linux added under `#else`.
- No new dependencies.
- Local commits authorized, on `main`, never push.

---

### Task 1: Platform shims

- [x] `linux/wincompat.h`: `__cdecl` (empty on GCC), `HWND`/`HMODULE`/`HINSTANCE` as `void*`.

### Task 2: Library loader

- [x] `AbstractLibrary`: guard `Imagehlp.h`/`GetFileArchitecture`; Linux uses `stat` + `dlopen`/`dlclose`.
- [x] `VSTPluginLibrary::loadFunctions`: `dlsym("VSTPluginMain")` with fallback `dlsym("main")`.
- [x] `getDefaultPluginPath`: `~/.vst` on Linux (registry path on Windows).

### Task 3: Instance + filter

- [x] `VSTPluginInstance`: guard `wincrypt.h`; portable base64 for chunk state; guard `initialize` SEH.
- [x] `VSTPluginFilter`: guard both `__try/__except` blocks; body runs unguarded on Linux.
- [x] `VSTPluginFilterFactory`: `std::filesystem` for relative plugin paths on Linux.
- [x] Fix `../Version.h` → `../version.h` (case-sensitive filesystem).

### Task 4: Stub plugin + test

- [x] `linux/vst-test/stub_plugin.cpp`: implements the in-repo `vst_effect_t` ABI, fixed 0.5 gain.
- [x] `tests/run_vst_e2e.sh`: runs `eqapo-null` with `VSTPlugin: Library <stub>` and asserts 0.5.

## Self-Review

- Spec §7 (native VST2) → Tasks 1–4.
- Windows VST hosting is unchanged; the `windows_untouched` test enforces this.
- The provided FabFilter corpus is Windows PE and cannot be `dlopen`ed directly; yabridge remains the optional bridge and is not a dependency.
