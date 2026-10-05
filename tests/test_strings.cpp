#include <cassert>
#include <cstddef>
#include <cstdio>
#include <string>
#include "helpers/StringHelper.h"

int main()
{
    assert(StringHelper::trim(L"  hi  ") == L"hi");
    auto parts = StringHelper::split(L"a;b;c", L';');
    assert(parts.size() == 3 && parts[1] == L"b");
    assert(StringHelper::join({L"a", L"b"}, L",") == L"a,b");
    assert(StringHelper::toLowerCase(L"AbC") == L"abc");
    std::wstring rt = StringHelper::toWString(StringHelper::toString(L"h\u00e9llo", 65001u), 65001u);
    assert(rt == L"h\u00e9llo");
    std::printf("OK\n");
    return 0;
}
