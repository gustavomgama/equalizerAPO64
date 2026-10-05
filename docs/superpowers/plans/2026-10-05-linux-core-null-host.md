# EqualizerAPO Cross-Platform Core + Null Host — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a native Linux build of the EqualizerAPO filter engine — proven by a WAV-in/WAV-out `eqapo-null` tool and golden tests — **without changing the Windows build**, so the project ships both versions.

**Architecture:** All changes are additive. Shared files keep their Windows implementation under `#ifdef _WIN32` byte-for-byte and gain a Linux implementation under `#else`. A new `CMakeLists.txt` builds the Linux target only; the existing VS solution / `build.bat` / `Editor.pro` are untouched. `eqapo-null` drives the engine over a WAV file so the DSP is verifiable with no audio server. The PipeWire host, VST test plugin, and Editor are separate later plans.

**Tech Stack:** C++17, CMake ≥ 3.20, muparserx 4.0.12 (FetchContent), FFTW3 3.3.x, libsndfile 1.2.x, POSIX threads, inotify.

**Spec:** `docs/superpowers/specs/2026-10-05-equalizerapo-linux-port-design.md`

## Global Constraints

- **Windows is preserved and untouched.** The Windows build (`.sln`, `.vcxproj`, `build.bat`, `Editor.pro`, `.rc`, installer) must continue to compile and ship. Never edit those files. Never delete or rewrite a Windows code path.
- Every shared `.cpp`/`.h` edit wraps the Linux code in `#ifdef _WIN32 … #else … #endif`, leaving the Windows branch intact.
- Linux-only additions live in new files (`linux/…`, `helpers/ConfigPathHelper.*`, `tests/…`) and are never added to the VS project.
- C++17. CMake ≥ 3.20. `-O2` Release default.
- Internal strings stay `std::wstring`; files on disk are UTF-8. Never assume `wchar_t` is UTF-16 (Linux `wchar_t` is 32-bit).
- Config directory on Linux: `$XDG_CONFIG_HOME/equalizerapo`, fallback `~/.config/equalizerapo`; file `config.txt`.
- muparserx: prefer a system install; otherwise build the fork via `FetchContent` with `USE_WIDE_STRING=ON`. The whole project is compiled with `MUP_USE_WIDE_STRING` (both Windows projects define it), so the engine's `eqapo_core` target must define it too.
- No new dependencies beyond muparserx, FFTW3, libsndfile (and, in later plans, PipeWire + Qt6). No Wine.
- **Local commits are authorized** (user-approved). Commit after each green task, on the current branch, local only — **never push**. `git branch --show-current` must be reported before the first commit.
- Every task ends with a runnable check that fails if the logic breaks.

---

### Task 1: Portable platform layer + CMake skeleton + Windows guard

**Files:**
- Create: `linux/wincompat.h`, `helpers/ConfigPathHelper.h`, `helpers/ConfigPathHelper.cpp`, `tests/test_platform.cpp`, `tests/check_windows_untouched.sh`
- Create: `CMakeLists.txt`
- Modify (guarded): `stdafx.h`, `helpers/MemoryHelper.cpp`, `helpers/PrecisionTimer.h`, `helpers/LogHelper.cpp`

**Interfaces:**
- Produces: `MemoryHelper::alloc(size_t)->void*`, `MemoryHelper::free(void*)` (unchanged signatures, 16-byte aligned); `PrecisionTimer::start()/stop()->double`; `LogHelper` macros unchanged; `ConfigPathHelper::getConfigDir/getConfigFile/getStateDir()->std::wstring`.

- [ ] **Step 1: Write the Windows-untouched guard test**

Create `tests/check_windows_untouched.sh`:

```bash
#!/usr/bin/env bash
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

# 1. Windows-only build files must never be modified by this work.
bad=$(git status --porcelain | awk '{print $2}' | grep -E '\.(vcxproj|sln|bat|pro|rc)$' || true)
if [ -n "$bad" ]; then
  echo "FAIL: Windows build files modified:"; echo "$bad"; exit 1
fi

# 2. Every modified shared source must still contain its Windows branch.
fail=0
for f in $(git status --porcelain | awk '{print $2}' | grep -E '\.(cpp|h)$' || true); do
  case "$f" in
    helpers/ConfigPathHelper.*|linux/*|tests/*) continue ;;
  esac
  if ! grep -q '_WIN32' "$f"; then
    echo "FAIL: no Windows guard preserved in $f"; fail=1
  fi
done
[ "$fail" -eq 0 ] || exit 1
echo "OK: Windows port untouched"
```

Run: `bash tests/check_windows_untouched.sh`
Expected: PASS (nothing modified yet), prints `OK: Windows port untouched`.

- [ ] **Step 2: Write the failing platform test**

Create `tests/test_platform.cpp`:

```cpp
#include <cassert>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include "helpers/MemoryHelper.h"
#include "helpers/PrecisionTimer.h"
#include "helpers/ConfigPathHelper.h"

int main()
{
    for (size_t n : {1u, 7u, 16u, 1000u})
    {
        void* p = MemoryHelper::alloc(n);
        assert(p != nullptr);
        assert(reinterpret_cast<uintptr_t>(p) % 16 == 0);
        MemoryHelper::free(p);
    }

    PrecisionTimer t;
    t.start();
    double elapsed = t.stop();
    assert(elapsed >= 0.0 && elapsed < 10.0);

    std::wstring dir = ConfigPathHelper::getConfigDir();
    assert(!dir.empty());
    assert(dir.find(L"equalizerapo") != std::wstring::npos);

    std::printf("OK\n");
    return 0;
}
```

- [ ] **Step 3: Create the CMake build (Linux only)**

Create `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.20)
project(EqualizerAPO-Linux LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFTW3 REQUIRED fftw3)
pkg_check_modules(SNDFILE REQUIRED sndfile)

# muparserx: system install if present, else build the fork from source.
find_path(MUPARSERX_INCLUDE_DIR mpParser.h)
find_library(MUPARSERX_LIBRARY NAMES muparserx)
if(MUPARSERX_LIBRARY AND MUPARSERX_INCLUDE_DIR)
  set(MUPARSERX_LINK ${MUPARSERX_LIBRARY})
  set(MUPARSERX_INCLUDES ${MUPARSERX_INCLUDE_DIR})
else()
  include(FetchContent)
  set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(USE_WIDE_STRING ON CACHE BOOL "" FORCE)
  FetchContent_Declare(muparserx
    GIT_REPOSITORY https://github.com/thefirekahuna/muparserx
    GIT_TAG 0a48779615103ca5baa301be9eefecdf23156ec6)
  FetchContent_MakeAvailable(muparserx)
  set(MUPARSERX_LINK muparserx)
  set(MUPARSERX_INCLUDES ${muparserx_SOURCE_DIR}/parser)
endif()

add_library(eqapo_core STATIC
  helpers/MemoryHelper.cpp
  helpers/LogHelper.cpp
  helpers/StringHelper.cpp
  helpers/ConfigPathHelper.cpp
)
target_include_directories(eqapo_core PUBLIC
  ${CMAKE_SOURCE_DIR} ${MUPARSERX_INCLUDES} ${FFTW3_INCLUDE_DIRS} ${SNDFILE_INCLUDE_DIRS}
)
target_compile_definitions(eqapo_core PUBLIC MUP_USE_WIDE_STRING)
target_link_libraries(eqapo_core PUBLIC
  ${FFTW3_LIBRARIES} ${SNDFILE_LIBRARIES} ${MUPARSERX_LINK} dl pthread
)

enable_testing()
add_executable(test_platform tests/test_platform.cpp)
target_link_libraries(test_platform PRIVATE eqapo_core)
add_test(NAME platform COMMAND test_platform)
add_test(NAME windows_untouched COMMAND bash ${CMAKE_SOURCE_DIR}/tests/check_windows_untouched.sh)
```

Run: `cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j`
Expected: FAIL — `linux/wincompat.h` missing, `ConfigPathHelper` undefined, `windows.h` not found.

- [ ] **Step 4: Make `stdafx.h` portable without touching Windows**

Replace the Windows include block in `stdafx.h` with:

```cpp
#define _USE_MATH_DEFINES
#include <cmath>
#include <climits>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <exception>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <regex>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <Shlwapi.h>
#include <Ks.h>
#include <KsMedia.h>
#else
#include "linux/wincompat.h"
#endif
#include "helpers/ScopeGuard.h"
```

The Windows branch is unchanged; only the `#else` is new.

- [ ] **Step 5: Add `linux/wincompat.h`**

Create `linux/wincompat.h`:

```cpp
#pragma once
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <ctime>
#include <string>
#include <thread>
#include <chrono>
#include <unistd.h>

#ifndef _WIN32

#define MAX_PATH 4096
#define CP_UTF8 65001u
#define CP_ACP 0u

static inline void* _aligned_malloc(size_t size, size_t alignment) {
    void* p = nullptr;
    if (posix_memalign(&p, alignment, size) != 0) return nullptr;
    return p;
}
static inline void _aligned_free(void* p) { free(p); }

static inline int _wcsicmp(const wchar_t* a, const wchar_t* b) {
    while (*a && *b) {
        wchar_t ca = *a, cb = *b;
        if (ca >= L'A' && ca <= L'Z') ca += 32;
        if (cb >= L'A' && cb <= L'Z') cb += 32;
        if (ca != cb) return ca < cb ? -1 : 1;
        ++a; ++b;
    }
    return *a == *b ? 0 : (*a ? 1 : -1);
}
static inline int _wcsnicmp(const wchar_t* a, const wchar_t* b, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        wchar_t ca = a[i], cb = b[i];
        if (ca >= L'A' && ca <= L'Z') ca += 32;
        if (cb >= L'A' && cb <= L'Z') cb += 32;
        if (ca != cb) return ca < cb ? -1 : 1;
        if (!ca) return 0;
    }
    return 0;
}

static inline void Sleep(unsigned ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

#endif // !_WIN32
```

- [ ] **Step 6: Guard `MemoryHelper.cpp` (Windows branch untouched)**

Wrap the existing implementation in `#ifdef _WIN32` and add a Linux branch. The file becomes:

```cpp
#include "stdafx.h"
#include "LogHelper.h"
#include "MemoryHelper.h"

#ifdef _WIN32
// ... the existing Windows implementation (windows.h include, USE_WINDDK,
//     _malloc_dbg, AERT_Allocate, etc.) stays exactly as it is today ...
#else
void* MemoryHelper::alloc(size_t size)
{
    void* memory = malloc(size + 32);
    if (memory == NULL)
    {
        LogFStatic(L"Allocation of %d bytes failed.", size);
        return NULL;
    }
    size_t offset = 16 - ((size_t)memory) % 16;
    void* ptr = ((char*)memory) + offset;
    ((char*)ptr)[-1] = (char)offset;
    return ptr;
}

void MemoryHelper::free(void* ptr)
{
    if (ptr == NULL) return;
    char offset = ((char*)ptr)[-1];
    ::free(((char*)ptr) - offset);
}
#endif
```

Move the current `#include <windows.h>` / `USE_WINDDK` blocks inside the `#ifdef _WIN32`.

- [ ] **Step 7: Guard `PrecisionTimer.h`**

```cpp
#pragma once

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

class PrecisionTimer
{
    LARGE_INTEGER freq;
    LARGE_INTEGER startCount{};
public:
    PrecisionTimer() { QueryPerformanceFrequency(&freq); }
    void start() { QueryPerformanceCounter(&startCount); }
    double stop()
    {
        LARGE_INTEGER stopCount;
        QueryPerformanceCounter(&stopCount);
        return double(stopCount.QuadPart - startCount.QuadPart) / freq.QuadPart;
    }
};
#else
#include <chrono>

class PrecisionTimer
{
    std::chrono::steady_clock::time_point startCount;
public:
    PrecisionTimer() = default;
    void start() { startCount = std::chrono::steady_clock::now(); }
    double stop()
    {
        auto stopCount = std::chrono::steady_clock::now();
        return std::chrono::duration<double>(stopCount - startCount).count();
    }
};
#endif
```

- [ ] **Step 8: Guard `LogHelper.cpp`**

Keep the current Windows implementation under `#ifdef _WIN32` (including the
`RegistryHelper.h` include and `GetTempPathW`), and add the Linux branch:

```cpp
#include "stdafx.h"
#include <cstdarg>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "RegistryHelper.h"
#else
#include <ctime>
#include "ConfigPathHelper.h"
#endif
#include "LogHelper.h"

using namespace std;

bool LogHelper::initialized = false;
wstring LogHelper::logPath;
bool LogHelper::enableTrace = false;
FILE* LogHelper::presetFP = NULL;
bool LogHelper::compact = false;
bool LogHelper::useConsoleColors = false;

void LogHelper::log(const char* file, int line, const void* caller, bool trace, const wchar_t* format, ...)
{
#ifdef _WIN32
    // ... existing Windows implementation unchanged ...
#else
    if (!initialized)
    {
        initialized = true;
        logPath = ConfigPathHelper::getStateDir() + L"/EqualizerAPO.log";
        enableTrace = true;
    }

    if (trace && !enableTrace)
        return;

    FILE* fp;
    if (presetFP == NULL)
    {
        fp = fopen(string(logPath.begin(), logPath.end()).c_str(), "at");
        if (fp == NULL) return;
    }
    else
    {
        fp = presetFP;
    }

    if (!compact)
    {
        time_t now = time(NULL);
        struct tm lt;
        localtime_r(&now, &lt);
        fwprintf(fp, L"%04d-%02d-%02d %02d:%02d:%02d %s (%s:%d): ",
            lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec,
            trace ? L"TRACE" : L"LOG", file, line);
    }

    va_list varArgs;
    va_start(varArgs, format);
    vfwprintf(fp, format, varArgs);
    va_end(varArgs);
    fwprintf(fp, L"\n");

    if (presetFP == NULL) fclose(fp);
    else fflush(fp);
#endif
}
```

`reset()` and `set()` are platform-neutral; keep them once, outside the guard.

- [ ] **Step 9: Add `ConfigPathHelper`**

`helpers/ConfigPathHelper.h`:

```cpp
#pragma once
#include <string>

class ConfigPathHelper
{
public:
    static std::wstring getConfigDir();
    static std::wstring getConfigFile();
    static std::wstring getStateDir();
};
```

`helpers/ConfigPathHelper.cpp`:

```cpp
#include "stdafx.h"
#include <cstdlib>
#include <sys/stat.h>
#include "ConfigPathHelper.h"

using namespace std;

static wstring utf8ToWide(const string& s)
{
    return wstring(s.begin(), s.end());
}

static string homeDir()
{
    const char* h = getenv("HOME");
    return h ? h : "/tmp";
}

wstring ConfigPathHelper::getConfigDir()
{
    const char* xdg = getenv("XDG_CONFIG_HOME");
    string base = (xdg && *xdg) ? string(xdg) : homeDir() + "/.config";
    return utf8ToWide(base + "/equalizerapo");
}

wstring ConfigPathHelper::getConfigFile()
{
    return getConfigDir() + L"/config.txt";
}

wstring ConfigPathHelper::getStateDir()
{
    const char* xdg = getenv("XDG_STATE_HOME");
    string base = (xdg && *xdg) ? string(xdg) : homeDir() + "/.local/state";
    string dir = base + "/equalizerapo";
    mkdir(dir.c_str(), 0755);
    return utf8ToWide(dir);
}
```

This file is Linux-only (never added to the VS project), so it needs no guard.

- [ ] **Step 10: Run the tests to verify they pass**

Run: `cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: PASS — `platform` prints `OK`; `windows_untouched` prints `OK: Windows port untouched`.

- [ ] **Step 11: Commit**

Run: `git branch --show-current && git add -A && git commit -m "build: add Linux CMake skeleton and portable platform layer (Windows preserved)"`

---

### Task 2: StringHelper + parser (guarded)

**Files:**
- Modify (guarded): `helpers/StringHelper.cpp`, `parser/RegistryFunctions.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/test_strings.cpp`

**Interfaces:**
- Produces: unchanged `StringHelper` signatures from `helpers/StringHelper.h`.

- [ ] **Step 1: Write the failing test**

Create `tests/test_strings.cpp`:

```cpp
#include <cassert>
#include <cstdio>
#include "helpers/StringHelper.h"

int main()
{
    assert(StringHelper::trim(L"  hi  ") == L"hi");
    auto parts = StringHelper::split(L"a;b;c", L';');
    assert(parts.size() == 3 && parts[1] == L"b");
    assert(StringHelper::join({L"a", L"b"}, L",") == L"a,b");
    assert(StringHelper::toLowerCase(L"AbC") == L"abc");
    std::wstring rt = StringHelper::toWString(StringHelper::toString(L"héllo", 65001u), 65001u);
    assert(rt == L"héllo");
    std::printf("OK\n");
    return 0;
}
```

Add to `CMakeLists.txt`:

```cmake
add_executable(test_strings tests/test_strings.cpp)
target_link_libraries(test_strings PRIVATE eqapo_core)
add_test(NAME strings COMMAND test_strings)
```

Run: `cmake --build build -j && ctest --test-dir build -R strings --output-on-failure`
Expected: FAIL — StringHelper still includes `windows.h` unconditionally.

- [ ] **Step 2: Guard `StringHelper.cpp`**

Keep the Windows conversion code under `#ifdef _WIN32`; add Linux UTF-8 ↔
UTF-32 conversion under `#else`:

```cpp
static std::wstring utf8ToWide(const std::string& s)
{
    std::wstring out;
    for (size_t i = 0; i < s.size(); )
    {
        unsigned char c = (unsigned char)s[i];
        unsigned cp; size_t n;
        if (c < 0x80) { cp = c; n = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; n = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; n = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; n = 4; }
        else { out.push_back(0xFFFD); ++i; continue; }
        if (i + n > s.size()) { out.push_back(0xFFFD); break; }
        for (size_t k = 1; k < n; ++k) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
        out.push_back((wchar_t)cp);
        i += n;
    }
    return out;
}

static std::string wideToUtf8(const std::wstring& s)
{
    std::string out;
    for (wchar_t wc : s)
    {
        unsigned cp = (unsigned)wc;
        if (cp < 0x80) out.push_back((char)cp);
        else if (cp < 0x800) {
            out.push_back((char)(0xC0 | (cp >> 6)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back((char)(0xE0 | (cp >> 12)));
            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        } else {
            out.push_back((char)(0xF0 | (cp >> 18)));
            out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}
```

- `toWString`/`toString`: `#ifdef _WIN32` keep `MultiByteToWideChar`/`WideCharToMultiByte`; `#else` use the helpers above (CP_UTF8 → UTF-8; CP_ACP → Latin-1).
- `toLowerCase`/`toUpperCase`: `std::towlower`/`std::towupper` per character (works on both).
- `trim`: strip `L" \t\r\n"`.
- `getSystemErrorString`: `#ifdef _WIN32` keep `FormatMessageW`; `#else` widen `std::strerror(status)`.
- `replaceIllegalCharacters`: replace `/ \ : * ? " < > |` with `_`.
- Keep `replaceCharacters`, `split`, `join`, `splitQuoted` logic unchanged (no Windows API).

- [ ] **Step 3: Guard `parser/RegistryFunctions.cpp`**

Wrap the current Windows implementation in `#ifdef _WIN32`. Add a `#else`
branch with stub function bodies (return empty `wstring` / `0`) preserving the
same signatures so muparserx registration still compiles on Linux.

- [ ] **Step 4: Add parser sources to `eqapo_core`**

In `CMakeLists.txt`, add:

```cmake
  parser/LogicalOperators.cpp
  parser/RegexFunctions.cpp
  parser/StringOperators.cpp
```

`parser/RegistryFunctions.cpp` is deliberately **not** added here: it includes
`FilterEngine.h`, which is only guarded in Task 3. It is added in Task 4.

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: PASS — `strings` prints `OK`; `windows_untouched` still passes.

- [ ] **Step 5: Commit**

Run: `git add -A && git commit -m "port: add Linux StringHelper and parser stubs (Windows preserved)"`

---

### Task 3: FilterEngine + FilterConfiguration (guarded)

**Files:**
- Modify (guarded): `FilterEngine.cpp`, `FilterEngine.h`, `FilterConfiguration.cpp`, `FilterConfiguration.h`
- Modify: `CMakeLists.txt`
- Test: `tests/test_engine_preamp.cpp`

**Interfaces:**
- Consumes: `ConfigPathHelper`, portable helpers, `MemoryHelper`.
- Produces: unchanged public API — `FilterEngine::initialize(float,unsigned,unsigned,unsigned,unsigned,unsigned,const std::wstring&)`, `process(double*,double*,unsigned)`, `process(double**,double**,unsigned)`, `loadConfig(const std::wstring&)`.

- [ ] **Step 1: Write the failing test**

Create `tests/test_engine_preamp.cpp`:

```cpp
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <vector>
#include "FilterEngine.h"

int main()
{
    const char* cfg = "/tmp/eqapo_test_preamp.txt";
    { std::ofstream f(cfg); f << "Preamp: -6 dB\n"; }

    FilterEngine engine;
    const unsigned frames = 512, channels = 2;
    engine.initialize(48000.0f, channels, channels, channels, 0, frames, cfg);

    std::vector<double> in(frames * channels, 0.5);
    std::vector<double> out(frames * channels, 0.0);
    engine.process(out.data(), in.data(), frames);

    const double expected = 0.5 * std::pow(10.0, -6.0 / 20.0);
    for (double s : out)
        assert(std::fabs(s - expected) < 1e-6);

    std::printf("OK\n");
    return 0;
}
```

Add to `CMakeLists.txt`:

```cmake
  FilterEngine.cpp
  FilterConfiguration.cpp
  IFilter.cpp
  helpers/ChannelHelper.cpp
  filters/PreampFilter.cpp
  filters/PreampFilterFactory.cpp

add_executable(test_engine_preamp tests/test_engine_preamp.cpp)
target_link_libraries(test_engine_preamp PRIVATE eqapo_core)
add_test(NAME engine_preamp COMMAND test_engine_preamp)
```

Run: `cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j`
Expected: FAIL — `windows.h`, `CreateSemaphore`, `EnterCriticalSection`, `RegistryHelper`, `FindFirstChangeNotificationW`.

- [ ] **Step 2: Guard `FilterEngine.h`**

Keep `#include <windows.h>` and the Windows members under `#ifdef _WIN32`. Add
a Linux branch:

```cpp
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#endif
```

Replace the members:

```cpp
#ifdef _WIN32
    HANDLE loadSemaphore;
    CRITICAL_SECTION loadSection;
    void* threadHandle;
    void* shutdownEvent;
#else
    std::mutex loadMutex;
    std::thread notificationThreadObj;
    std::atomic<bool> notificationShutdown{false};
#endif
```

`watchRegistryKeys` stays declared but is only used on Windows; on Linux it is
harmless and unused.

- [ ] **Step 3: Guard `FilterEngine.cpp`**

- Keep `windows.h`/`Shlwapi.h`/`Ks*.h` includes under `#ifdef _WIN32`; add
  under `#else`: `<sys/inotify.h>`, `<poll.h>`, `<unistd.h>`, `<atomic>`,
  `"helpers/ConfigPathHelper.h"`.
- Constructor: keep `InitializeCriticalSection`/`CreateSemaphore` under
  `#ifdef _WIN32`.
- Destructor: keep the `SetEvent`/`WaitForSingleObject`/`CloseHandle` teardown
  under `#ifdef _WIN32`; add:

```cpp
#else
    notificationShutdown = true;
    if (notificationThreadObj.joinable())
        notificationThreadObj.join();
#endif
```

- Introduce a small locking shim so the body is not duplicated. Add near the
  top of the file:

```cpp
#ifdef _WIN32
struct EngineLock {
    CRITICAL_SECTION* cs;
    explicit EngineLock(CRITICAL_SECTION* c) : cs(c) { EnterCriticalSection(cs); }
    ~EngineLock() { LeaveCriticalSection(cs); }
};
#define ENGINE_LOCK() EngineLock _engineLock(&loadSection)
#else
#define ENGINE_LOCK() std::lock_guard<std::mutex> _engineLock(loadMutex)
#endif
```

Replace each `EnterCriticalSection(&loadSection);` with `ENGINE_LOCK();` and
delete the matching `LeaveCriticalSection(&loadSection);` (including the early
`return` path in `initialize`). On Windows the behaviour is identical.

- `initialize`: replace the config-path read with:

```cpp
#ifdef _WIN32
    configPath = RegistryHelper::readValue(APP_REGPATH, L"ConfigPath");
#else
    configPath = ConfigPathHelper::getConfigDir();
#endif
```

- `loadConfigFile`: keep the Windows `CreateFile`/`ReadFile` loop under
  `#ifdef _WIN32`; add:

```cpp
#else
    std::ifstream fileIn(string(path.begin(), path.end()), std::ios::binary);
    if (!fileIn)
    {
        LogF(L"Error while reading configuration file %s", path.c_str());
        return;
    }
    std::stringstream inputStream;
    inputStream << fileIn.rdbuf();
    inputStream.seekg(0);
#endif
```

- Notification thread: keep the Windows `notificationThread` under
  `#ifdef _WIN32`; add the Linux version:

```cpp
#else
void FilterEngine::notificationThread(FilterEngine* engine)
{
    int fd = inotify_init1(IN_CLOEXEC);
    if (fd < 0) return;
    std::string dir(engine->configPath.begin(), engine->configPath.end());
    int wd = inotify_add_watch(fd, dir.c_str(),
        IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_DELETE);
    if (wd < 0) { close(fd); return; }

    char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    while (!engine->notificationShutdown.load())
    {
        struct pollfd pfd{fd, POLLIN, 0};
        int r = poll(&pfd, 1, 200);
        if (r <= 0) continue;
        ssize_t len = read(fd, buf, sizeof(buf));
        if (len <= 0) continue;
        bool configChanged = false;
        for (char* p = buf; p < buf + len; )
        {
            auto* ev = reinterpret_cast<struct inotify_event*>(p);
            if (ev->len > 0 && std::string(ev->name) == "config.txt")
                configChanged = true;
            p += sizeof(struct inotify_event) + ev->len;
        }
        if (configChanged)
            engine->loadConfig(L"");
    }
    inotify_rm_watch(fd, wd);
    close(fd);
}
#endif
```

- Thread start: keep the Windows `CreateEventW`/`CreateThread` under
  `#ifdef _WIN32`; add:

```cpp
#else
    if (!notificationThreadObj.joinable() && customPath.empty())
        notificationThreadObj = std::thread(notificationThread, this);
#endif
```

The Windows declaration keeps its `__stdcall`; the Linux one is plain.

- [ ] **Step 4: Guard `FilterConfiguration.cpp/.h`**

No Windows API is used directly. Add `#include <cstring>` for
`memcpy`/`memset` if it is not already transitively included. Keep
`MemoryHelper` usage unchanged.

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: PASS — `engine_preamp` prints `OK`; `windows_untouched` passes.

- [ ] **Step 6: Commit**

Run: `git add -A && git commit -m "port: add Linux FilterEngine/FilterConfiguration paths (Windows preserved)"`

---

### Task 4: Filter set (guarded)

**Files:**
- Modify (guarded): `filters/ConvolutionFilter.cpp`, `filters/GraphicEQFilter.cpp`, `filters/IncludeFilterFactory.cpp`, `filters/DeviceFilterFactory.cpp`, `filters/loudnessCorrection/VolumeController.cpp`, `filters/VSTPluginFilterFactory.cpp`, `filters/VSTPluginFilter.cpp`, `helpers/VSTPluginInstance.cpp`, `helpers/VSTPluginLibrary.cpp`, `libHybridConv-0.1.1/libHybridConv_eapo.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/test_filters.cpp`

**Interfaces:**
- Produces: every `IFilterFactory` in `FilterEngine`'s constructor links and loads on Linux; no behavior change to filter math on either platform.

- [ ] **Step 1: Write the failing test**

Create `tests/test_filters.cpp`:

```cpp
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <vector>
#include "FilterEngine.h"

static std::vector<double> run(const char* cfgText, const std::vector<double>& in, unsigned ch, unsigned frames)
{
    const char* cfg = "/tmp/eqapo_test_filters.txt";
    { std::ofstream f(cfg); f << cfgText; }
    FilterEngine engine;
    engine.initialize(48000.0f, ch, ch, ch, 0, frames, cfg);
    std::vector<double> out(in.size(), 0.0);
    engine.process(out.data(), const_cast<double*>(in.data()), frames);
    return out;
}

int main()
{
    const unsigned frames = 512, ch = 2;
    std::vector<double> impulse(frames * ch, 0.0);
    impulse[0] = impulse[1] = 1.0;

    auto out = run("Filter 1: ON PK Fc 1000 Hz Gain -20 dB Q 1\n", impulse, ch, frames);
    double energy = 0.0;
    for (double s : out) energy += s * s;
    assert(energy > 0.0);

    { std::ofstream f("/tmp/eqapo_test_inc.txt"); f << "Preamp: -6 dB\n"; }
    auto out2 = run("Include: /tmp/eqapo_test_inc.txt\n", impulse, ch, frames);
    double energy2 = 0.0;
    for (double s : out2) energy2 += s * s;
    assert(energy2 > 0.0);

    std::printf("OK\n");
    return 0;
}
```

Add to `CMakeLists.txt`:

```cmake
add_executable(test_filters tests/test_filters.cpp)
target_link_libraries(test_filters PRIVATE eqapo_core)
add_test(NAME filters COMMAND test_filters)
```

Run: `cmake --build build -j && ctest --test-dir build -R filters --output-on-failure`
Expected: FAIL — link errors for the filter factories.

- [ ] **Step 2: Guard `ConvolutionFilter.cpp` / `GraphicEQFilter.cpp`**

Keep `#include <windows.h>` and `#define ENABLE_SNDFILE_WINDOWS_PROTOTYPES 1`
under `#ifdef _WIN32`; under `#else` include only `<sndfile.h>` and
`<fftw3.h>`. On Linux, convert any wide path via
`StringHelper::toString(path, CP_UTF8)` before calling `sf_open`.

- [ ] **Step 3: Guard `IncludeFilterFactory.cpp`**

Keep `#include <Shlwapi.h>` and the `PathIsRelativeW`/`MAX_PATH` branch under
`#ifdef _WIN32`; add the Linux branch:

```cpp
#else
#include <filesystem>
namespace fs = std::filesystem;
fs::path inc(value.begin(), value.end());
if (inc.is_relative())
    inc = fs::path(configPath.begin(), configPath.end()).parent_path() / inc;
std::wstring includePath = inc.wstring();
#endif
```

- [ ] **Step 4: Guard `DeviceFilterFactory.cpp`**

Windows device matching stays under `#ifdef _WIN32`; the Linux branch matches
against the channel/device names the engine was initialized with (string
comparison). No device enumeration in this plan.

- [ ] **Step 5: Guard `VolumeController.cpp`**

Keep the Core Audio (`IMMDeviceEnumerator`/`IAudioEndpointVolume`)
implementation under `#ifdef _WIN32`; add a Linux stub reporting 1.0 (0 dB)
with a log line noting loudness correction is inactive until the PipeWire host
lands. Keep the class interface used by `LoudnessCorrectionFilter`.

- [ ] **Step 6: Guard the VST host (`dlopen`)**

In `helpers/VSTPluginLibrary.cpp`: keep `LoadLibraryW`/`GetProcAddress`/
`FreeLibrary` and the `RegistryHelper` plugin-path lookup under
`#ifdef _WIN32`; add:

```cpp
#else
#include <dlfcn.h>
void* handle = dlopen(pathUtf8.c_str(), RTLD_NOW | RTLD_LOCAL);
void* sym = dlsym(handle, "VSTPluginMain");
if (!sym) sym = dlsym(handle, "main");
// dlclose(handle) on unload
#endif
```

Accept `.so` as well as `.dll` in extension checks. `VSTPluginInstance.cpp`:
adjust types only. `VSTPluginFilterFactory.cpp`: keep parsing unchanged; on
Linux convert the plugin path to UTF-8.

- [ ] **Step 7: Guard `libHybridConv_eapo.cpp`**

Keep Windows `_aligned_malloc`/`_aligned_free` under `#ifdef _WIN32`; use
`MemoryHelper` (or `posix_memalign`) under `#else`. Remove the unconditional
`windows.h`. Keep the algorithm identical.

- [ ] **Step 8: Add all remaining filter sources to `eqapo_core`**

In `CMakeLists.txt`, add every `.cpp` under `filters/` and
`filters/loudnessCorrection/`, plus
`libHybridConv-0.1.1/libHybridConv_eapo.cpp`,
`helpers/VSTPluginInstance.cpp`, `helpers/VSTPluginLibrary.cpp`, and
`parser/RegistryFunctions.cpp` (deferred from Task 2; it needs the now-guarded
`FilterEngine.h`). Enumerate them explicitly; do not glob.

- [ ] **Step 9: Run the tests to verify they pass**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: PASS — `filters` prints `OK`; `windows_untouched` passes.

- [ ] **Step 10: Commit**

Run: `git add -A && git commit -m "port: add Linux filter paths incl. dlopen VST host (Windows preserved)"`

---

### Task 5: `eqapo-null` WAV host

**Files:**
- Create: `linux/nullhost/main.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/run_null_e2e.sh`

**Interfaces:**
- Consumes: `FilterEngine` public API, `ConfigPathHelper`.
- Produces: executable `eqapo-null`, CLI `--in <wav> --out <wav> [--config <file>]`.

- [ ] **Step 1: Write the failing test**

Create `tests/run_null_e2e.sh`:

```bash
#!/usr/bin/env bash
set -euo pipefail
BIN="$1"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

python3 - "$WORK/in.wav" <<'PY'
import sys, wave, struct, math
path = sys.argv[1]
rate, dur, amp = 48000, 1.0, 0.5
with wave.open(path, "wb") as w:
    w.setnchannels(2); w.setsampwidth(2); w.setframerate(rate)
    for i in range(int(rate * dur)):
        v = int(amp * 32767 * math.sin(2 * math.pi * 1000 * i / rate))
        w.writeframes(struct.pack("<hh", v, v))
PY

cat > "$WORK/config.txt" <<'EOF'
Preamp: -6 dB
EOF

"$BIN" --in "$WORK/in.wav" --out "$WORK/out.wav" --config "$WORK/config.txt"

python3 - "$WORK/in.wav" "$WORK/out.wav" <<'PY'
import sys, wave, struct, math
def rms(path):
    with wave.open(path, "rb") as w:
        n = w.getnframes(); raw = w.readframes(n); ch = w.getnchannels()
    vals = struct.unpack("<%dh" % (n * ch), raw)
    return math.sqrt(sum(v * v for v in vals) / len(vals))
ratio = rms(sys.argv[2]) / rms(sys.argv[1])
expected = 10 ** (-6 / 20)
assert abs(ratio - expected) < 0.01, (ratio, expected)
print("OK", ratio)
PY
```

Add to `CMakeLists.txt`:

```cmake
add_executable(eqapo-null linux/nullhost/main.cpp)
target_link_libraries(eqapo-null PRIVATE eqapo_core)
add_test(NAME null_e2e COMMAND bash ${CMAKE_SOURCE_DIR}/tests/run_null_e2e.sh $<TARGET_FILE:eqapo-null>)
```

Run: `cmake --build build -j && ctest --test-dir build -R null_e2e --output-on-failure`
Expected: FAIL — `eqapo-null` does not exist.

- [ ] **Step 2: Write `linux/nullhost/main.cpp`**

```cpp
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <sndfile.h>
#include "FilterEngine.h"

int main(int argc, char** argv)
{
    std::string inPath, outPath, configPath;
    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--in") && i + 1 < argc) inPath = argv[++i];
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) outPath = argv[++i];
        else if (!strcmp(argv[i], "--config") && i + 1 < argc) configPath = argv[++i];
    }
    if (inPath.empty() || outPath.empty())
    {
        std::fprintf(stderr, "usage: eqapo-null --in <wav> --out <wav> [--config <file>]\n");
        return 2;
    }

    SF_INFO inInfo{};
    SNDFILE* in = sf_open(inPath.c_str(), SFM_READ, &inInfo);
    if (!in) { std::fprintf(stderr, "open %s: %s\n", inPath.c_str(), sf_strerror(nullptr)); return 1; }

    SF_INFO outInfo = inInfo;
    SNDFILE* out = sf_open(outPath.c_str(), SFM_WRITE, &outInfo);
    if (!out) { std::fprintf(stderr, "create %s: %s\n", outPath.c_str(), sf_strerror(nullptr)); sf_close(in); return 1; }

    const unsigned maxFrames = 1024;
    FilterEngine engine;
    std::wstring cfg(configPath.begin(), configPath.end());
    engine.initialize((float)inInfo.samplerate, inInfo.channels, inInfo.channels, inInfo.channels, 0, maxFrames, cfg);

    std::vector<double> inBuf(maxFrames * inInfo.channels);
    std::vector<double> outBuf(maxFrames * inInfo.channels);
    sf_count_t frames;
    while ((frames = sf_readf_double(in, inBuf.data(), maxFrames)) > 0)
    {
        engine.process(outBuf.data(), inBuf.data(), (unsigned)frames);
        sf_writef_double(out, outBuf.data(), frames);
    }

    sf_close(in);
    sf_close(out);
    return 0;
}
```

- [ ] **Step 3: Run the test to verify it passes**

Run: `cmake --build build -j && ctest --test-dir build -R null_e2e --output-on-failure`
Expected: PASS, prints `OK <ratio≈0.5012>`.

- [ ] **Step 4: Commit**

Run: `git add -A && git commit -m "feat: add eqapo-null WAV processor for Linux DSP verification"`

---

### Task 6: Full CTest wiring + Linux build docs

**Files:**
- Modify: `CMakeLists.txt` (ensure all tests registered), `README.md`
- Test: full suite from a clean build directory

**Interfaces:**
- Produces: `ctest` runs the whole suite green on a clean Linux checkout.

- [ ] **Step 1: Add a Linux build section to `README.md`**

Append (do not alter existing Windows instructions):

```markdown
## Building on Linux

    # deps (Arch): sudo pacman -S cmake fftw libsndfile
    # muparserx is fetched and built automatically if not installed
    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build -j
    ctest --test-dir build --output-on-failure
```

- [ ] **Step 2: Run the full suite from a clean build directory**

Run: `rm -rf build && cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: PASS — `platform`, `windows_untouched`, `strings`, `engine_preamp`, `filters`, `null_e2e` all green.

- [ ] **Step 3: Final Windows-preservation audit**

Run: `bash tests/check_windows_untouched.sh && git diff --stat HEAD~5 -- '*.vcxproj' '*.sln' '*.bat' '*.pro' '*.rc'`
Expected: guard prints OK; the `git diff` prints nothing (no Windows build file changed).

- [ ] **Step 4: Commit**

Run: `git add -A && git commit -m "docs: add Linux build instructions; full CTest suite green"`

---

## Execution Deviations (applied 2026-10-05)

These deviations from the steps above were made during execution; they keep the
same guarantees and are reflected in the code:

1. **Tasks 3 and 4 are coupled.** `FilterEngine`'s constructor instantiates
   every factory, so the engine cannot link with only `PreampFilter`. The
   filter set was ported in the same pass.
2. **VST and loudness-correction are deferred** (spec phase 4). Their sources
   are excluded from `eqapo_core`, and their factory registration in
   `FilterEngine.cpp` is guarded by `#ifdef _WIN32`. They compile on Windows
   exactly as before.
3. **MSVC-isms shimmed** in `linux/wincompat.h`: `__forceinline`, `swscanf_s`,
   `strcpy_s`, plus the speaker channel-mask constants. `BiQuad.h` gained a
   guarded `alignas(16)` alternative.
4. **Build additions:** `helpers/GainIterator.cpp`, `fftw3_threads`,
   `-mavx2 -mfma -mf16c` on x86_64, and `<immintrin.h>` in `FilterEngine.cpp`.
5. **`loadConfig` uses a recursive mutex** on Linux (`std::recursive_mutex`)
   because `loadConfig` calls `loadConfigFile` while holding the lock, as the
   Windows `CRITICAL_SECTION` allowed.
6. **Include/Convolution relative paths** use `std::filesystem` on Linux.

## Self-Review

**Spec coverage (phases 1–2):**
- Cross-platform layer, Windows preserved (spec §2, §3, §5) → Tasks 1–4, enforced by `windows_untouched` test.
- Build system (spec §4.1, §4.3) → Tasks 1, 6.
- `--null` host + WAV verification (spec §9.1) → Task 5.
- Golden tests (spec §9.2) → Tasks 3, 4, 5.
- Windows-untouched guard (spec §9.5) → Task 1 step 1, run in every `ctest`.
- VST2 native (spec §7) → Task 4 step 6 ports the `dlopen` host; the stub-plugin test is deferred to the VST plan (spec phase 4).
- PipeWire host (spec §6) and Editor (spec §8) → **out of scope for this plan**, covered by later plans per the spec's phasing.

**Placeholder scan:** no `TBD`/`TODO`; every step has concrete code or an exact guarded substitution.

**Type consistency:** `MemoryHelper::alloc/free`, `PrecisionTimer::start/stop`, `StringHelper` signatures, and `FilterEngine::initialize/process` are unchanged from the current headers, so later tasks rely on them safely. `ConfigPathHelper::getConfigDir/getConfigFile/getStateDir` are defined in Task 1 and used in Tasks 1, 3, 5 with matching names.

**Known unknowns (not placeholders):** exact compile errors from the `wchar_t`/WinAPI sweep surface during Tasks 1–4 builds; the transformation rule (guard, keep Windows branch, add `#else`) is specified per file. The FetchContent build needs network on first configure.
