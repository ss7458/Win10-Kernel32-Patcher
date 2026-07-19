// DllProvider.cpp - DLL search path API compatibility implementations.
// Strategy: All functions are L0 (benign success) — they accept input, discard it,
//           and return a success value so that callers can proceed normally.
//           Full DLL search path emulation is not feasible from a compatibility
//           shim, but returning success satisfies the vast majority of callers.

#include "../CompatRuntime.h"

// ============================================================
// AddDllDirectory - L0 (benign success)
// Introduced: Win8 / Win10
// Adds a directory to the process DLL search path. We return a
// non-null cookie so callers can later remove it via RemoveDllDirectory.
// ============================================================
COMPAT_API DLL_DIRECTORY_COOKIE WINAPI AddDllDirectory(PCWSTR NewDirectory)
{
    (void)NewDirectory;
    return (DLL_DIRECTORY_COOKIE)1;
}

// ============================================================
// RemoveDllDirectory - L0 (benign success)
// Introduced: Win8 / Win10
// Removes a directory previously added with AddDllDirectory.
// ============================================================
COMPAT_API BOOL WINAPI RemoveDllDirectory(DLL_DIRECTORY_COOKIE Cookie)
{
    (void)Cookie;
    return TRUE;
}

// ============================================================
// SetDefaultDllDirectories - L0 (benign success)
// Introduced: Win8 / Win10 / KB2533623
// Sets the default DLL search directories for the process.
// ============================================================
COMPAT_API BOOL WINAPI SetDefaultDllDirectories(DWORD DirectoryFlags)
{
    (void)DirectoryFlags;
    return TRUE;
}
