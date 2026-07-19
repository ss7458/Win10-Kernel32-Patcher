// ThreadProvider.cpp - Thread API compatibility implementations.
// Strategy: SetThreadDescription/GetThreadDescription -> L0 (return S_OK/null)
//           SetThreadInformation/GetThreadInformation -> L2 (dispatch by class)

#include "../CompatRuntime.h"

// ============================================================
// SetThreadDescription - L0 (cosmetic, return S_OK)
// Introduced: Win10 1607 (build 14393)
// ============================================================
COMPAT_API HRESULT WINAPI SetThreadDescription(HANDLE hThread, PCWSTR lpThreadDescription)
{
    (void)hThread;
    (void)lpThreadDescription;
    return S_OK;
}

// ============================================================
// GetThreadDescription - L0 (return nullptr, no description)
// Introduced: Win10 1607 (build 14393)
// ============================================================
COMPAT_API HRESULT WINAPI GetThreadDescription(HANDLE hThread, PWSTR* ppszThreadDescription)
{
    if (!ppszThreadDescription) return E_INVALIDARG;
    *ppszThreadDescription = nullptr;
    (void)hThread;
    return S_OK;
}

// ============================================================
// SetThreadInformation - L2 (dispatch by class)
// Introduced: Win10 1607 (build 14393)
// ============================================================
COMPAT_API BOOL WINAPI SetThreadInformation(HANDLE hThread, DWORD ThreadInformationClass, LPVOID lpThreadInformation, DWORD dwSize)
{
    switch (ThreadInformationClass)
    {
    case 0: // MemoryPriority -> SetThreadPriority
    {
        if (!lpThreadInformation || dwSize < sizeof(DWORD)) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
        DWORD priority = *(DWORD*)lpThreadInformation;
        int tp = THREAD_PRIORITY_NORMAL;
        if (priority <= 1) tp = THREAD_PRIORITY_LOWEST;
        else if (priority <= 3) tp = THREAD_PRIORITY_BELOW_NORMAL;
        else if (priority <= 5) tp = THREAD_PRIORITY_NORMAL;
        else tp = THREAD_PRIORITY_ABOVE_NORMAL;
        return SetThreadPriority(hThread, tp);
    }
    case 1: // AbsoluteCpuPriority -> SetThreadPriority
    {
        if (!lpThreadInformation || dwSize < sizeof(DWORD)) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
        DWORD cpu = *(DWORD*)lpThreadInformation;
        int tp = THREAD_PRIORITY_NORMAL;
        if (cpu <= 4) tp = THREAD_PRIORITY_LOWEST;
        else if (cpu <= 8) tp = THREAD_PRIORITY_BELOW_NORMAL;
        else if (cpu <= 12) tp = THREAD_PRIORITY_NORMAL;
        else tp = THREAD_PRIORITY_ABOVE_NORMAL;
        return SetThreadPriority(hThread, tp);
    }
    default:
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
}

// ============================================================
// GetThreadInformation - L2 (dispatch by class)
// Introduced: Win10 1607 (build 14393)
// ============================================================
COMPAT_API BOOL WINAPI GetThreadInformation(HANDLE hThread, DWORD ThreadInformationClass, LPVOID lpThreadInformation, DWORD dwSize)
{
    switch (ThreadInformationClass)
    {
    case 0: // MemoryPriority
    {
        if (!lpThreadInformation || dwSize < sizeof(DWORD)) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
        int tp = GetThreadPriority(hThread);
        DWORD priority = 5;
        if (tp <= THREAD_PRIORITY_LOWEST) priority = 1;
        else if (tp <= THREAD_PRIORITY_BELOW_NORMAL) priority = 3;
        else if (tp <= THREAD_PRIORITY_NORMAL) priority = 5;
        else priority = 9;
        *(DWORD*)lpThreadInformation = priority;
        return TRUE;
    }
    case 1: // AbsoluteCpuPriority
    {
        if (!lpThreadInformation || dwSize < sizeof(DWORD)) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
        int tp = GetThreadPriority(hThread);
        DWORD cpu = 8;
        if (tp <= THREAD_PRIORITY_LOWEST) cpu = 4;
        else if (tp <= THREAD_PRIORITY_BELOW_NORMAL) cpu = 6;
        else if (tp <= THREAD_PRIORITY_NORMAL) cpu = 8;
        else cpu = 12;
        *(DWORD*)lpThreadInformation = cpu;
        return TRUE;
    }
    default:
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
}
