#include <cassert>
#include <cstddef>
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
