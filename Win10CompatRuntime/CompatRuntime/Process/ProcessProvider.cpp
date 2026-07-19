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

// ============================================================
// GetProcessInformation - L2 (forward to real API)
// Introduced: Win8
// ============================================================
COMPAT_API BOOL WINAPI GetProcessInformation(HANDLE hProcess, PROCESS_INFORMATION_CLASS ProcessInformationClass, LPVOID ProcessInformation, DWORD ProcessInformationSize)
{
    (void)ProcessInformationClass;
    (void)ProcessInformation;
    (void)ProcessInformationSize;
    auto pReal = Compat_GetRealProc("kernel32", "GetProcessInformation");
    if (pReal)
        return ((BOOL(WINAPI*)(HANDLE, PROCESS_INFORMATION_CLASS, LPVOID, DWORD))pReal)(hProcess, ProcessInformationClass, ProcessInformation, ProcessInformationSize);
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

// ============================================================
// SetProcessInformation - L2 (forward to real API)
// Introduced: Win8
// ============================================================
COMPAT_API BOOL WINAPI SetProcessInformation(HANDLE hProcess, PROCESS_INFORMATION_CLASS ProcessInformationClass, LPVOID ProcessInformation, DWORD ProcessInformationSize)
{
    (void)ProcessInformationClass;
    (void)ProcessInformation;
    (void)ProcessInformationSize;
    auto pReal = Compat_GetRealProc("kernel32", "SetProcessInformation");
    if (pReal)
        return ((BOOL(WINAPI*)(HANDLE, PROCESS_INFORMATION_CLASS, LPVOID, DWORD))pReal)(hProcess, ProcessInformationClass, ProcessInformation, ProcessInformationSize);
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

// ============================================================
// GetProcessMitigationPolicy - L4 (forward or memset + error)
// Introduced: Win10 1607 (build 14393)
// ============================================================
COMPAT_API BOOL WINAPI GetProcessMitigationPolicy(HANDLE hProcess, PROCESS_MITIGATION_POLICY MitigationPolicy, PVOID lpBuffer, SIZE_T dwLength)
{
    (void)hProcess;
    (void)MitigationPolicy;
    auto pReal = Compat_GetRealProc("kernel32", "GetProcessMitigationPolicy");
    if (pReal)
        return ((BOOL(WINAPI*)(HANDLE, PROCESS_MITIGATION_POLICY, PVOID, SIZE_T))pReal)(hProcess, MitigationPolicy, lpBuffer, dwLength);
    if (lpBuffer)
        memset(lpBuffer, 0, dwLength);
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

// ============================================================
// SetProcessMitigationPolicy - L3 (forward or benign ignore)
// Introduced: Win10 1607 (build 14393)
// ============================================================
COMPAT_API BOOL WINAPI SetProcessMitigationPolicy(PROCESS_MITIGATION_POLICY MitigationPolicy, PVOID lpBuffer, SIZE_T dwLength)
{
    (void)MitigationPolicy;
    (void)lpBuffer;
    (void)dwLength;
    auto pReal = Compat_GetRealProc("kernel32", "SetProcessMitigationPolicy");
    if (pReal)
        return ((BOOL(WINAPI*)(PROCESS_MITIGATION_POLICY, PVOID, SIZE_T))pReal)(MitigationPolicy, lpBuffer, dwLength);
    return TRUE;
}

// ============================================================
// IsProcessCritical - L1 (forward or return FALSE)
// Introduced: Win10 1607 (build 14393)
// ============================================================
COMPAT_API BOOL WINAPI IsProcessCritical(HANDLE hProcess, PBOOL Critical)
{
    (void)hProcess;
    auto pReal = Compat_GetRealProc("kernel32", "IsProcessCritical");
    if (pReal)
        return ((BOOL(WINAPI*)(HANDLE, PBOOL))pReal)(hProcess, Critical);
    if (Critical)
        *Critical = FALSE;
    return TRUE;
}

// ============================================================
// QueryProcessAffinityUpdateMode - L1 (forward or return 0)
// Introduced: Win10 1607 (build 14393)
// ============================================================
COMPAT_API BOOL WINAPI QueryProcessAffinityUpdateMode(HANDLE hProcess, PDWORD lpdwFlags)
{
    (void)hProcess;
    auto pReal = Compat_GetRealProc("kernel32", "QueryProcessAffinityUpdateMode");
    if (pReal)
        return ((BOOL(WINAPI*)(HANDLE, PDWORD))pReal)(hProcess, lpdwFlags);
    if (lpdwFlags)
        *lpdwFlags = 0;
    return TRUE;
}

// ============================================================
// SetProcessAffinityUpdateMode - L1 (forward or benign ignore)
// Introduced: Win10 1607 (build 14393)
// ============================================================
COMPAT_API BOOL WINAPI SetProcessAffinityUpdateMode(HANDLE hProcess, DWORD dwFlags)
{
    (void)hProcess;
    (void)dwFlags;
    auto pReal = Compat_GetRealProc("kernel32", "SetProcessAffinityUpdateMode");
    if (pReal)
        return ((BOOL(WINAPI*)(HANDLE, DWORD))pReal)(hProcess, dwFlags);
    return TRUE;
}
