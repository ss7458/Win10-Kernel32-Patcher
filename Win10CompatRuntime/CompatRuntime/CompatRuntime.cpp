// CompatRuntime.cpp - DLL entry point and initialization for the
// Windows 10 Compatibility Runtime.
//
// All compatibility API implementations live in the per-subsystem
// provider .cpp files (Thread/, Memory/, File/, ...). This file only
// provides the DLL lifecycle (DllMain) and any global initialization.

#include "CompatRuntime.h"
#include <stdio.h>

#ifdef COMPATRUNTIME_EXPORTS
// Defined when building the DLL so COMPAT_API expands to dllexport.
#endif

static BOOL g_Initialized = FALSE;

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved)
{
    switch (fdwReason)
    {
    case DLL_PROCESS_ATTACH:
        // Disable thread-attached notifications for performance; we don't need them.
        DisableThreadLibraryCalls(hinstDLL);
        g_Initialized = TRUE;
        break;

    case DLL_PROCESS_DETACH:
        g_Initialized = FALSE;
        break;

    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;
    }
    return TRUE;
}

// Optional diagnostic helper. Not part of any Windows API; used by tests
// and by the patch tool to confirm the runtime is loaded.
extern "C" COMPAT_API DWORD WINAPI CompatRuntime_GetVersion()
{
    // Returns the compatibility runtime version (1.0.0 -> 0x01000000).
    return 0x01000000;
}
