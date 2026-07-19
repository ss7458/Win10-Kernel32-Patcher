// ProcessProvider.cpp - Process API compatibility implementations.
// QueryFullProcessImageNameW -> L1 (fallback to GetModuleFileNameExW via psapi)
// GetPackageFamilyName -> L3 (stub, not on desktop)
// GetCurrentPackageId -> L3 (stub, returns ERROR_NOT_FOUND)
// GetCurrentApplicationUserModelId -> L3 (stub)

#include "../CompatRuntime.h"
#include <stdio.h>

// ============================================================
// QueryFullProcessImageNameW - L1 (fallback to GetModuleFileNameW)
// Introduced: Win10 1607 (build 14393)
// ============================================================
COMPAT_API BOOL WINAPI QueryFullProcessImageNameW(HANDLE hProcess, DWORD dwFlags, LPWSTR lpExeName, PDWORD lpdwSize)
{
    if (!lpExeName || !lpdwSize) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    // GetModuleFileNameW only works for current process; for other processes
    // we use psapi. Most callers use GetCurrentProcess().
    DWORD pid = GetProcessId(hProcess);
    if (pid == GetCurrentProcessId())
    {
        DWORD len = GetModuleFileNameW(NULL, lpExeName, *lpdwSize);
        if (len == 0 || len >= *lpdwSize) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
        *lpdwSize = len;
        return TRUE;
    }
    // For other processes, try EnumProcessModules + GetModuleFileNameExW
    typedef DWORD (WINAPI *GetModuleFileNameExW_t)(HANDLE, HMODULE, LPWSTR, DWORD);
    static GetModuleFileNameExW_t pFunc = NULL;
    if (!pFunc)
    {
        HMODULE psapi = GetModuleHandleA("psapi.dll");
        if (!psapi) psapi = LoadLibraryA("psapi.dll");
        if (psapi) pFunc = (GetModuleFileNameExW_t)GetProcAddress(psapi, "GetModuleFileNameExW");
    }
    if (pFunc)
    {
        DWORD len = pFunc(hProcess, NULL, lpExeName, *lpdwSize);
        if (len == 0 || len >= *lpdwSize) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
        *lpdwSize = len;
        return TRUE;
    }
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

// ============================================================
// GetPackageFamilyName - L3 (stub: we are not a packaged app)
// Introduced: Win10 1607 (build 14393)
// ============================================================
COMPAT_API LONG WINAPI GetPackageFamilyName(HANDLE hProcess, UINT32* packageFamilyNameLength, PWSTR packageFamilyName)
{
    (void)hProcess;
    (void)packageFamilyNameLength;
    (void)packageFamilyName;
    return ERROR_NOT_FOUND; // Not a packaged app
}

// ============================================================
// GetCurrentPackageId - L3 (stub)
// Introduced: Win10 1607 (build 14393)
// ============================================================
COMPAT_API LONG WINAPI GetCurrentPackageId(UINT32* bufferLength, BYTE* buffer)
{
    (void)bufferLength;
    (void)buffer;
    return ERROR_NOT_FOUND; // Not a packaged app
}

// ============================================================
// GetCurrentApplicationUserModelId - L3 (stub)
// Introduced: Win10 1607 (build 14393)
// ============================================================
COMPAT_API LONG WINAPI GetCurrentApplicationUserModelId(UINT32* applicationUserModelIdLength, PWSTR applicationUserModelId)
{
    (void)applicationUserModelIdLength;
    (void)applicationUserModelId;
    return ERROR_NOT_FOUND; // Not a packaged app
}
