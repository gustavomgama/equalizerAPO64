# EqualizerAPO Linux PipeWire Host — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A native PipeWire client (`eqapo-host`) that exposes a virtual sink, runs the EqualizerAPO engine on its audio, and forwards the result to the default output — giving system-wide EQ with hot-reload.

**Architecture:** Two `pw_stream`s in one process. An **input stream** declared `media.class = Audio/Sink` is the virtual sink apps route to; its RT process callback runs `FilterEngine::process()` and pushes processed frames into a lock-free SPSC ring buffer. An **output stream** (`Stream/Output/Audio`, `AUTOCONNECT`) drains the ring to the default sink. Both are clocked by the same PipeWire graph, so no resampling/drift handling is needed. `FilterEngine`'s existing inotify watcher reloads `config.txt` on change. Windows is untouched; this is a Linux-only target.

**Tech Stack:** libpipewire-0.3 (1.6.x), spa, the existing `eqapo_core`, C++17, CMake.

**Spec:** `docs/superpowers/specs/2026-10-05-equalizerapo-linux-port-design.md` (§6)

## Global Constraints

- Linux-only target (`linux/host/`); never added to the Windows VS project.
- Reuse `eqapo_core`; no changes to the engine API.
- RT callbacks must not allocate or lock: pre-allocate buffers; the ring uses atomics.
- `eqapo_core` is compiled `-mavx2 -mfma -mf16c` on x86_64 (already set).
- Local commits authorized, on `main`, never push.

---

### Task 1: Host skeleton + output stream

**Files:**
- Create: `linux/host/main.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add the target**

```cmake
pkg_check_modules(PIPEWIRE REQUIRED libpipewire-0.3)
add_executable(eqapo-host linux/host/main.cpp)
target_link_libraries(eqapo-host PRIVATE eqapo_core ${PIPEWIRE_LIBRARIES})
target_include_directories(eqapo-host PRIVATE ${PIPEWIRE_INCLUDE_DIRS})
```

- [ ] **Step 2: Implement `main.cpp`** (see the implementation committed alongside this plan): parse `--config`/`--name`/`--rate`/`--channels`, build a `FilterEngine`, open the output stream with `PW_STREAM_FLAG_AUTOCONNECT`, open the input stream as `Audio/Sink`, and pump the ring in both process callbacks.

- [ ] **Step 3: Build**

Run: `cmake -B build && cmake --build build -j`
Expected: `eqapo-host` builds.

### Task 2: Integration check

**Files:**
- Create: `tests/run_host_e2e.sh`

- [ ] **Step 1: Write the test**

Starts `eqapo-host` in the background, waits for the node to appear in `pw-cli ls Node`, asserts a link from the host output to the default sink exists via `pw-link -l`, then kills the host.

- [ ] **Step 2: Run it**

Run: `ctest --test-dir build -R host_e2e --output-on-failure`
Expected: node present, link present.

## Self-Review

- Spec §6 (PipeWire host) → Tasks 1–2.
- Hot-reload is inherited from `FilterEngine`'s inotify watcher (already ported).
- Multi-device `Device:` routing is deliberately left to a later iteration (spec §12 open question); this plan ships one virtual sink.
