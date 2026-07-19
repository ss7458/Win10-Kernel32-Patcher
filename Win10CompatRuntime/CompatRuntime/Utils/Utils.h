// Utils.h - Shared helper utilities for compatibility providers.
//
// These helpers let providers forward to the real OS implementation when it
// exists, fall back to older APIs, or report the host OS version. They keep
// individual provider code small and consistent.

#pragma once

#include "CompatRuntime.h"

namespace compat {
namespace utils {

// Returns TRUE if the host OS already exports `api` from `module`.
// When TRUE, providers should forward the call to avoid re-implementing
// behavior the OS already provides.
inline BOOL OsProvides(const char* module, const char* api)
{
    return Compat_GetRealProc(module, api) != nullptr;
}

// Returns the host OS build number (e.g. 15063, 19045), or 0 on failure.
inline DWORD HostBuild()
{
    return Compat_GetWindowsBuild();
}

// Returns TRUE if the host build is at least `minBuild`.
inline BOOL HostAtLeast(DWORD minBuild)
{
    DWORD b = HostBuild();
    return b != 0 && b >= minBuild;
}

// Allocates a UTF-16 string copy. Caller must free with CompatFreeString.
inline PWSTR DuplicateString(PCWSTR src)
{
    if (!src) return nullptr;
    size_t len = wcslen(src) + 1;
    PWSTR out = (PWSTR)HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR));
    if (out) wcscpy_s(out, len, src);
    return out;
}

inline void FreeString(PWSTR s)
{
    if (s) HeapFree(GetProcessHeap(), 0, s);
}

} // namespace utils
} // namespace compat
