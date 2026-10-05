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

// Windows speaker-position channel-mask constants (standard
// WAVEFORMATEXTENSIBLE values), used by ChannelHelper.
#define SPEAKER_FRONT_LEFT 0x1
#define SPEAKER_FRONT_RIGHT 0x2
#define SPEAKER_FRONT_CENTER 0x4
#define SPEAKER_LOW_FREQUENCY 0x8
#define SPEAKER_BACK_LEFT 0x10
#define SPEAKER_BACK_RIGHT 0x20
#define SPEAKER_FRONT_LEFT_OF_CENTER 0x40
#define SPEAKER_FRONT_RIGHT_OF_CENTER 0x80
#define SPEAKER_BACK_CENTER 0x100
#define SPEAKER_SIDE_LEFT 0x200
#define SPEAKER_SIDE_RIGHT 0x400
#define SPEAKER_TOP_CENTER 0x800
#define SPEAKER_TOP_FRONT_LEFT 0x1000
#define SPEAKER_TOP_FRONT_CENTER 0x2000
#define SPEAKER_TOP_FRONT_RIGHT 0x4000
#define SPEAKER_TOP_BACK_LEFT 0x8000
#define SPEAKER_TOP_BACK_CENTER 0x10000
#define SPEAKER_TOP_BACK_RIGHT 0x20000
#define KSAUDIO_SPEAKER_MONO (SPEAKER_FRONT_CENTER)
#define KSAUDIO_SPEAKER_STEREO (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT)
#define KSAUDIO_SPEAKER_QUAD \
    (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT)
#define KSAUDIO_SPEAKER_SURROUND \
    (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER | SPEAKER_LOW_FREQUENCY)
#define KSAUDIO_SPEAKER_5POINT1 \
    (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER | SPEAKER_LOW_FREQUENCY | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT)
#define KSAUDIO_SPEAKER_5POINT1_SURROUND \
    (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER | SPEAKER_LOW_FREQUENCY | SPEAKER_SIDE_LEFT | SPEAKER_SIDE_RIGHT)
#define KSAUDIO_SPEAKER_7POINT1_SURROUND \
    (KSAUDIO_SPEAKER_5POINT1_SURROUND | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT)

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

// MSVC-isms used in the shared DSP code.
#ifndef __forceinline
#define __forceinline inline __attribute__((always_inline))
#endif

// aeffectx.h declares function pointers with __cdecl; GCC/x86-64 has a single
// calling convention, so it is empty here.
#ifndef __cdecl
#define __cdecl
#endif

// Minimal handle typedefs used by the VST host classes.
typedef void* HWND;
typedef void* HMODULE;
typedef void* HINSTANCE;

#define swscanf_s swscanf

// Bounded string copy matching the strcpy_s contract used by the VST code.
static inline int eqapo_strcpy_s(char* dst, size_t dstSize, const char* src) {
    if (!dst || dstSize == 0) return -1;
    if (!src) { dst[0] = '\0'; return 0; }
    strncpy(dst, src, dstSize - 1);
    dst[dstSize - 1] = '\0';
    return 0;
}
#define strcpy_s eqapo_strcpy_s

#endif // !_WIN32
