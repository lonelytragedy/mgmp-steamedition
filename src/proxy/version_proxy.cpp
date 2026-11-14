#include <windows.h>
#include <cstdint>
#include <cwchar>

#include "version_exports.h"

extern "C" uintptr_t mgmp_ver_table[];

namespace mgmp_proxy {

bool load_system_version() {
    wchar_t path[MAX_PATH] = {};
    UINT n = GetSystemDirectoryW(path, MAX_PATH);
    if (!n || n > MAX_PATH - 16) return false;
    wcscat_s(path, L"\\version.dll");

    HMODULE real = LoadLibraryW(path);
    if (!real) return false;

    const size_t count = sizeof(kVersionExports) / sizeof(kVersionExports[0]);
    for (size_t i = 0; i < count; ++i) {
        FARPROC f = GetProcAddress(real, kVersionExports[i]);
        if (!f) return false;
        mgmp_ver_table[i] = (uintptr_t)f;
    }
    return true;
}

}
