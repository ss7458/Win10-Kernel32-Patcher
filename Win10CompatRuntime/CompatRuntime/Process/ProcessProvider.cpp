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

// ============================================================
// IsProcessInJob - L1 (forward, fallback via NtQueryInformationProcess)
// Introduced: Win10 1607 (build 14393) [API exists since Vista but often
// imported from kernelbase on Win10]
// ============================================================
COMPAT_API BOOL WINAPI IsProcessInJob(HANDLE hProcess, HANDLE hJob, PBOOL Result)
{
    typedef BOOL(WINAPI* PFN)(HANDLE, HANDLE, PBOOL);
    static PFN pfn = nullptr;
    if (!pfn) pfn = (PFN)Compat_GetRealProc("kernel32", "IsProcessInJob");
    if (pfn) return pfn(hProcess, hJob, Result);

    if (!Result) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    // Fallback: use NtQueryInformationProcess with ProcessBasicInformation
    // to check if the process is in any job. This is a best-effort check.
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (!ntdll) ntdll = LoadLibraryA("ntdll.dll");
    if (ntdll)
    {
        typedef LONG(NTAPI* NtQueryInfo_t)(HANDLE, ULONG, PVOID, ULONG, PULONG);
        auto NtQueryInfo = (NtQueryInfo_t)GetProcAddress(ntdll, "NtQueryInformationProcess");
        if (NtQueryInfo)
        {
            // ProcessBasicInformation = 0, but we need ProcessIsInJob (7)
            ULONG ret = 0;
            ULONGLONG inJob = 0;
            // ProcessIsInJob = 7
            LONG st = NtQueryInfo(hProcess, 7, &inJob, sizeof(inJob), &ret);
            if (st == 0)
            {
                *Result = inJob ? TRUE : FALSE;
                return TRUE;
            }
        }
    }
    *Result = FALSE;
    return TRUE;
}

// ============================================================
// GetProcessDefaultCpuSetMasks - L1 (forward, fallback returns no masks)
// Introduced: Win10 1903 (build 18362)
// ============================================================
COMPAT_API BOOL WINAPI GetProcessDefaultCpuSetMasks(HANDLE hProcess, PGROUP_AFFINITY CpuSetMasks, USHORT CpuSetMaskCount, PUSHORT RequiredMaskCount)
{
    typedef BOOL(WINAPI* PFN)(HANDLE, PGROUP_AFFINITY, USHORT, PUSHORT);
    static PFN pfn = nullptr;
    if (!pfn) pfn = (PFN)Compat_GetRealProc("kernel32", "GetProcessDefaultCpuSetMasks");
    if (pfn) return pfn(hProcess, CpuSetMasks, CpuSetMaskCount, RequiredMaskCount);

    // Fallback: CPU sets don't exist on older systems. Report that zero
    // masks are needed (i.e. no restriction), which is the correct default.
    if (RequiredMaskCount) *RequiredMaskCount = 0;
    return TRUE;
}

// ============================================================
// SetProcessDefaultCpuSetMasks - L1 (forward, fallback benign ignore)
// Introduced: Win10 1903 (build 18362)
// ============================================================
COMPAT_API BOOL WINAPI SetProcessDefaultCpuSetMasks(HANDLE hProcess, PGROUP_AFFINITY CpuSetMasks, USHORT CpuSetMaskCount)
{
    typedef BOOL(WINAPI* PFN)(HANDLE, PGROUP_AFFINITY, USHORT);
    static PFN pfn = nullptr;
    if (!pfn) pfn = (PFN)Compat_GetRealProc("kernel32", "SetProcessDefaultCpuSetMasks");
    if (pfn) return pfn(hProcess, CpuSetMasks, CpuSetMaskCount);

    // Fallback: CPU sets not supported; silently accept (no-op).
    return TRUE;
}

// ============================================================
// GetProcessMitigationPolicy - already implemented above; no duplicate.
// ============================================================

// ============================================================
// GetThreadSelectedCpuSets - L1 (forward, fallback returns all CPUs)
// Introduced: Win10 1903 (build 18362)
// ============================================================
COMPAT_API BOOL WINAPI GetThreadSelectedCpuSets(HANDLE hThread, PULONG CpuSetIds, ULONG CpuSetIdCount, PULONG RequiredIdCount)
{
    typedef BOOL(WINAPI* PFN)(HANDLE, PULONG, ULONG, PULONG);
    static PFN pfn = nullptr;
    if (!pfn) pfn = (PFN)Compat_GetRealProc("kernel32", "GetThreadSelectedCpuSets");
    if (pfn) return pfn(hThread, CpuSetIds, CpuSetIdCount, RequiredIdCount);

    // Fallback: no CPU set restriction; report zero required IDs.
    if (RequiredIdCount) *RequiredIdCount = 0;
    return TRUE;
}

// ============================================================
// SetThreadSelectedCpuSets - L1 (forward, fallback benign ignore)
// Introduced: Win10 1903 (build 18362)
// ============================================================
COMPAT_API BOOL WINAPI SetThreadSelectedCpuSets(HANDLE hThread, const ULONG* CpuSetIds, ULONG CpuSetIdCount)
{
    typedef BOOL(WINAPI* PFN)(HANDLE, const ULONG*, ULONG);
    static PFN pfn = nullptr;
    if (!pfn) pfn = (PFN)Compat_GetRealProc("kernel32", "SetThreadSelectedCpuSets");
    if (pfn) return pfn(hThread, CpuSetIds, CpuSetIdCount);

    // Fallback: CPU sets not supported; silently accept (no-op).
    return TRUE;
}

// ============================================================
// GetStartupInfoW - L1 (forward, fallback to GetStartupInfoA + conversion)
// Introduced: Win10 1607 (build 14393) [kernelbase variant]
// ============================================================
COMPAT_API VOID WINAPI GetStartupInfoW(LPSTARTUPINFOW lpStartupInfo)
{
    typedef VOID(WINAPI* PFN)(LPSTARTUPINFOW);
    static PFN pfn = nullptr;
    if (!pfn) pfn = (PFN)Compat_GetRealProc("kernel32", "GetStartupInfoW");
    if (pfn) { pfn(lpStartupInfo); return; }

    // Fallback: use the ANSI version and convert.
    STARTUPINFOA sia = { 0 };
    sia.cb = sizeof(sia);
    GetStartupInfoA(&sia);
    if (lpStartupInfo)
    {
        lpStartupInfo->cb = sizeof(STARTUPINFOW);
        lpStartupInfo->lpReserved = nullptr;
        lpStartupInfo->lpDesktop = nullptr;
        lpStartupInfo->lpTitle = nullptr;
        lpStartupInfo->dwX = sia.dwX;
        lpStartupInfo->dwY = sia.dwY;
        lpStartupInfo->dwXSize = sia.dwXSize;
        lpStartupInfo->dwYSize = sia.dwYSize;
        lpStartupInfo->dwXCountChars = sia.dwXCountChars;
        lpStartupInfo->dwYCountChars = sia.dwYCountChars;
        lpStartupInfo->dwFillAttribute = sia.dwFillAttribute;
        lpStartupInfo->dwFlags = sia.dwFlags;
        lpStartupInfo->wShowWindow = sia.wShowWindow;
        lpStartupInfo->cbReserved2 = sia.cbReserved2;
        lpStartupInfo->lpReserved2 = sia.lpReserved2;
        lpStartupInfo->hStdInput = sia.hStdInput;
        lpStartupInfo->hStdOutput = sia.hStdOutput;
        lpStartupInfo->hStdError = sia.hStdError;
    }
}
