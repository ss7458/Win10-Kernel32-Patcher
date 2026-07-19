// CompatRuntime.h - Core definitions for the Windows 10 Compatibility Runtime
//
// This header defines the common macros, includes, and the provider
// registration interface used by all compatibility API providers.
//
// Design goals:
//   - Every compat API is exported from CompatRuntime.dll with the SAME name
//     as the original Windows API (e.g. GetThreadDescription) so that
//     CompatPatch can redirect imports transparently.
//   - Providers are organized by subsystem (Thread, Memory, File, ...) and
//     each implements a set of APIs following a uniform pattern.
//   - Compatibility levels (L0-L4) document how completely an API is emulated.

#pragma once

#ifndef COMPAT_RUNTIME_H
#define COMPAT_RUNTIME_H

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winternl.h>
#endif

// Export macro: every compat API uses this so the linker emits the export
// by its original Windows name. Exports are driven by CompatRuntime.def
// (module-definition file) instead of __declspec(dllexport); this avoids
// C2375/C2491 linkage conflicts when redefining SDK-declared APIs.
#define COMPAT_API extern "C"

// Benign: we intentionally redefine some Windows APIs (the SDK headers
// declare them with dllimport); the redeclaration differs only in linkage.
#pragma warning(disable: 4273)

// Compatibility level documentation (informational, not enforced at runtime).
//   L0 - Full compatibility: 100% faithful implementation.
//   L1 - Degraded compatibility: uses an older API, slightly less functionality.
//   L2 - Capability dispatch: behavior depends on parameters.
//   L3 - Stub: returns success / benign fake data so the caller proceeds.
//   L4 - Not implementable: returns ERROR_CALL_NOT_IMPLEMENTED.
#define COMPAT_LEVEL_0 0
#define COMPAT_LEVEL_1 1
#define COMPAT_LEVEL_2 2
#define COMPAT_LEVEL_3 3
#define COMPAT_LEVEL_4 4

// Helper: returns TRUE if the running OS already provides the named API
// via GetProcAddress on the given module. Used by providers that can simply
// forward to the real implementation when it exists.
inline FARPROC Compat_GetRealProc(const char* module, const char* api)
{
    HMODULE hMod = GetModuleHandleA(module);
    if (!hMod) hMod = LoadLibraryA(module);
    if (!hMod) return nullptr;
    return GetProcAddress(hMod, api);
}

// Helper: get the running Windows build number (e.g. 15063, 19045).
// Returns 0 on failure.
inline DWORD Compat_GetWindowsBuild()
{
    DWORD build = 0;
    // Use RtlGetVersion to avoid application compatibility shims.
    typedef LONG(NTAPI * RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (!ntdll) return 0;
    auto RtlGetVersion = (RtlGetVersionPtr)GetProcAddress(ntdll, "RtlGetVersion");
    if (!RtlGetVersion) return 0;
    RTL_OSVERSIONINFOW info = { 0 };
    info.dwOSVersionInfoSize = sizeof(info);
    if (RtlGetVersion(&info) == 0)
    {
        build = info.dwBuildNumber;
    }
    return build;
}

// Internal: apply runtime IAT redirection to the current process. Called from
// DllMain when injected by CompatLoader.exe (COMPAT_LOADER env flag set).
void Compat_RuntimePatchCurrentProcess(const char* dbDir);

// Exported: apply runtime IAT redirection to the current process on demand.
COMPAT_API void WINAPI Compat_ApplyToCurrentProcess(const char* dbDir);

#endif // COMPAT_RUNTIME_H
