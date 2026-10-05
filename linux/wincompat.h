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
