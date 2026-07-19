// AdvApiProvider.cpp - Advanced Windows API compatibility implementations (AdvApi32 subset).
// Strategy: ETW calls (EventRegister/EventWrite/EventUnregister) -> L3 stub
//           AppModel calls (GetCurrentApplicationUserModelId/GetPackageFamilyName) -> L4 not-implementable

#include "../CompatRuntime.h"
#include <evntprov.h>

// ============================================================
// EventRegister - L3 (stub, returns success with a dummy handle)
// Introduced: Vista
// ============================================================
COMPAT_API ULONG WINAPI EventRegister(LPCGUID ProviderId, PENABLECALLBACK EnableCallback, PVOID CallbackContext, PREGHANDLE RegHandle)
{
    auto real = (ULONG(WINAPI*)(LPCGUID, PENABLECALLBACK, PVOID, PREGHANDLE))
        Compat_GetRealProc("advapi32", "EventRegister");
    if (real)
        return real(ProviderId, EnableCallback, CallbackContext, RegHandle);

    if (RegHandle)
        *RegHandle = (REGHANDLE)1;
    (void)ProviderId;
    (void)EnableCallback;
    (void)CallbackContext;
    return ERROR_SUCCESS;
}

// ============================================================
// EventWrite - L3 (stub, returns success)
// Introduced: Vista
// ============================================================
COMPAT_API ULONG WINAPI EventWrite(REGHANDLE RegHandle, PCEVENT_DESCRIPTOR EventDescriptor, ULONG UserDataCount, PEVENT_DATA_DESCRIPTOR UserData)
{
    auto real = (ULONG(WINAPI*)(REGHANDLE, PCEVENT_DESCRIPTOR, ULONG, PEVENT_DATA_DESCRIPTOR))
        Compat_GetRealProc("advapi32", "EventWrite");
    if (real)
        return real(RegHandle, EventDescriptor, UserDataCount, UserData);

    (void)RegHandle;
    (void)EventDescriptor;
    (void)UserDataCount;
    (void)UserData;
    return ERROR_SUCCESS;
}

// ============================================================
// EventUnregister - L3 (stub, returns success)
// Introduced: Vista
// ============================================================
COMPAT_API ULONG WINAPI EventUnregister(REGHANDLE RegHandle)
{
    auto real = (ULONG(WINAPI*)(REGHANDLE))
        Compat_GetRealProc("advapi32", "EventUnregister");
    if (real)
        return real(RegHandle);

    (void)RegHandle;
    return ERROR_SUCCESS;
}


