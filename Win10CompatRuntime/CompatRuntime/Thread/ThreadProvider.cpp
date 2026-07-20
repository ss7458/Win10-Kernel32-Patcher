// ThreadProvider.cpp - Thread API compatibility implementations.
// Strategy: SetThreadDescription/GetThreadDescription -> L0 (real store/retrieve)
//           SetThreadInformation/GetThreadInformation -> L2 (dispatch by class)
// 
#include "../CompatRuntime.h"
#include <unordered_map>
#include <string>

// Per-thread description storage, keyed by thread ID. Protected by a SRW lock.
static SRWLOCK g_tdLock = SRWLOCK_INIT;
static std::unordered_map<DWORD, std::wstring> g_threadDesc;

// ============================================================
// SetThreadDescription - L0 (real store, keyed by thread ID)
// Introduced: Win10 1607 (build 14393)
// ============================================================
COMPAT_API HRESULT WINAPI SetThreadDescription(HANDLE hThread, PCWSTR lpThreadDescription)
{
    DWORD tid = GetThreadId(hThread);
    if (tid == 0)
        return HRESULT_FROM_WIN32(GetLastError());
    std::wstring wstr(lpThreadDescription ? lpThreadDescription : L"");
    AcquireSRWLockExclusive(&g_tdLock);
    g_threadDesc[tid] = std::move(wstr);
    ReleaseSRWLockExclusive(&g_tdLock);
    return S_OK;
}

// ============================================================
// GetThreadDescription - L0 (real retrieve; caller frees with LocalFree)
// Introduced: Win10 1607 (build 14393)
// ============================================================
COMPAT_API HRESULT WINAPI GetThreadDescription(HANDLE hThread, PWSTR* ppszThreadDescription)
{
    if (!ppszThreadDescription)
        return E_INVALIDARG;
    *ppszThreadDescription = nullptr;
    DWORD tid = GetThreadId(hThread);
    if (tid == 0)
        return HRESULT_FROM_WIN32(GetLastError());

    AcquireSRWLockExclusive(&g_tdLock);
    auto it = g_threadDesc.find(tid);
    if (it != g_threadDesc.end())
    {
        const std::wstring& s = it->second;
        SIZE_T bytes = (s.size() + 1) * sizeof(wchar_t);
        PWSTR buf = (PWSTR)LocalAlloc(LMEM_FIXED, bytes);
        if (buf)
            wcscpy_s(buf, s.size() + 1, s.c_str());
        *ppszThreadDescription = buf;
    }
    ReleaseSRWLockExclusive(&g_tdLock);
    return S_OK;
}

// ============================================================
// SetThreadInformation - L2 (forward to real API, fallback dispatch by class)
// Introduced: Win10 1607 (build 14393)
// ============================================================
COMPAT_API BOOL WINAPI SetThreadInformation(HANDLE hThread, THREAD_INFORMATION_CLASS ThreadInformationClass, LPVOID lpThreadInformation, DWORD dwSize)
{
    typedef BOOL(WINAPI* PFN)(HANDLE, THREAD_INFORMATION_CLASS, LPVOID, DWORD);
    static PFN pfn = nullptr;
    if (!pfn) pfn = (PFN)Compat_GetRealProc("kernel32", "SetThreadInformation");
    if (pfn) return pfn(hThread, ThreadInformationClass, lpThreadInformation, dwSize);

    // Fallback for systems without the API: map known classes to thread priority.
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
// GetThreadInformation - L2 (forward to real API, fallback dispatch by class)
// Introduced: Win10 1607 (build 14393)
// ============================================================
COMPAT_API BOOL WINAPI GetThreadInformation(HANDLE hThread, THREAD_INFORMATION_CLASS ThreadInformationClass, LPVOID lpThreadInformation, DWORD dwSize)
{
    typedef BOOL(WINAPI* PFN)(HANDLE, THREAD_INFORMATION_CLASS, LPVOID, DWORD);
    static PFN pfn = nullptr;
    if (!pfn) pfn = (PFN)Compat_GetRealProc("kernel32", "GetThreadInformation");
    if (pfn) return pfn(hThread, ThreadInformationClass, lpThreadInformation, dwSize);

    // Fallback for systems without the API: map known classes to thread priority.
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
