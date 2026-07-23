// SyncProvider.cpp - Synchronization API compatibility implementations.
// Condition Variables are NOT trivial wrappers around CriticalSection.
// We must provide a real (albeit simplified) implementation using Events.

#include "../CompatRuntime.h"
#include <stdlib.h>

// Internal representation of a condition variable.
// Uses a manual-reset event as the underlying waitable object.
typedef struct _COMPAT_CV {
    HANDLE hEvent;
} COMPAT_CV;

// ============================================================
// InitializeConditionVariable - L0 (real implementation)
// Introduced: Win10 1607 (build 14393)
// ============================================================
COMPAT_API VOID WINAPI InitializeConditionVariable(PCONDITION_VARIABLE ConditionVariable)
{
    if (!ConditionVariable) return;
    // Idempotent: if already initialized, destroy old CV first.
    // The real Windows API treats re-initialization as a no-op; we leak the
    // previous allocation to avoid racing with active waiters, which matches
    // documented Windows behavior (undefined if misused).
    if (ConditionVariable->Ptr)
    {
        COMPAT_CV* old = (COMPAT_CV*)ConditionVariable->Ptr;
        if (old->hEvent) CloseHandle(old->hEvent);
        HeapFree(GetProcessHeap(), 0, old);
    }
    COMPAT_CV* cv = (COMPAT_CV*)HeapAlloc(GetProcessHeap(), 0, sizeof(COMPAT_CV));
    if (cv) cv->hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    ConditionVariable->Ptr = cv;
}

// ============================================================
// WakeConditionVariable - L0
// ============================================================
COMPAT_API VOID WINAPI WakeConditionVariable(PCONDITION_VARIABLE ConditionVariable)
{
    if (!ConditionVariable || !ConditionVariable->Ptr) return;
    COMPAT_CV* cv = (COMPAT_CV*)ConditionVariable->Ptr;
    SetEvent(cv->hEvent);
}

// ============================================================
// WakeAllConditionVariable - L0
// ============================================================
COMPAT_API VOID WINAPI WakeAllConditionVariable(PCONDITION_VARIABLE ConditionVariable)
{
    if (!ConditionVariable || !ConditionVariable->Ptr) return;
    COMPAT_CV* cv = (COMPAT_CV*)ConditionVariable->Ptr;
    SetEvent(cv->hEvent);
}

// ============================================================
// SleepConditionVariableCS - L0 (real implementation)
// ============================================================
COMPAT_API BOOL WINAPI SleepConditionVariableCS(PCONDITION_VARIABLE ConditionVariable,
    PCRITICAL_SECTION lpCriticalSection, DWORD dwMilliseconds)
{
    if (!ConditionVariable || !ConditionVariable->Ptr || !lpCriticalSection)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    COMPAT_CV* cv = (COMPAT_CV*)ConditionVariable->Ptr;
    // Reset the event before waiting.
    ResetEvent(cv->hEvent);
    // Leave the critical section, wait on the event, re-enter.
    LeaveCriticalSection(lpCriticalSection);
    DWORD result = WaitForSingleObject(cv->hEvent, dwMilliseconds);
    EnterCriticalSection(lpCriticalSection);
    if (result == WAIT_OBJECT_0) return TRUE;
    SetLastError(result == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError());
    return FALSE;
}

// ============================================================
// SleepConditionVariableSRW - L0 (real implementation)
// ============================================================
COMPAT_API BOOL WINAPI SleepConditionVariableSRW(PCONDITION_VARIABLE ConditionVariable,
    PSRWLOCK SRWLock, DWORD dwMilliseconds, ULONG Flags)
{
    if (!ConditionVariable || !ConditionVariable->Ptr || !SRWLock)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    COMPAT_CV* cv = (COMPAT_CV*)ConditionVariable->Ptr;
    ResetEvent(cv->hEvent);
    if (Flags & CONDITION_VARIABLE_LOCKMODE_SHARED)
    {
        ReleaseSRWLockShared(SRWLock);
        DWORD result = WaitForSingleObject(cv->hEvent, dwMilliseconds);
        AcquireSRWLockShared(SRWLock);
        if (result == WAIT_OBJECT_0) return TRUE;
        SetLastError(result == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError());
    }
    else
    {
        ReleaseSRWLockExclusive(SRWLock);
        DWORD result = WaitForSingleObject(cv->hEvent, dwMilliseconds);
        AcquireSRWLockExclusive(SRWLock);
        if (result == WAIT_OBJECT_0) return TRUE;
        SetLastError(result == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError());
    }
    return FALSE;
}

// File-scope state for WaitOnAddress family shims.
static SRWLOCK g_waSRW = SRWLOCK_INIT;
static CONDITION_VARIABLE g_waCV = CONDITION_VARIABLE_INIT;
static bool g_waCVInit = false;

static void Compat_EnsureWaCV()
{
    if (!g_waCVInit)
    {
        InitializeConditionVariable(&g_waCV);
        g_waCVInit = true;
    }
}

// ============================================================
// WaitOnAddress - L2 (approximation using SRW + CV)
// Introduced: Win8 / Win10
// ============================================================
COMPAT_API BOOL WINAPI WaitOnAddress(VOID volatile *Address, PVOID CompareAddress, SIZE_T AddressSize, DWORD dwMilliseconds)
{
    typedef BOOL(WINAPI* WaitOnAddressFn)(VOID volatile*, PVOID, SIZE_T, DWORD);
    auto realFn = (WaitOnAddressFn)Compat_GetRealProc("kernel32", "WaitOnAddress");
    if (realFn) return realFn(Address, CompareAddress, AddressSize, dwMilliseconds);

    Compat_EnsureWaCV();
    AcquireSRWLockExclusive(&g_waSRW);
    while (true) {
        if (memcmp((void*)Address, CompareAddress, AddressSize) == 0) {
            BOOL result = SleepConditionVariableSRW(&g_waCV, &g_waSRW, dwMilliseconds, 0);
            if (!result) {
                ReleaseSRWLockExclusive(&g_waSRW);
                return FALSE;
            }
        } else {
            ReleaseSRWLockExclusive(&g_waSRW);
            return TRUE;
        }
    }
}

// ============================================================
// WakeByAddressAll - L0
// ============================================================
COMPAT_API VOID WINAPI WakeByAddressAll(PVOID Address)
{
    (void)Address;
    Compat_EnsureWaCV();
    AcquireSRWLockExclusive(&g_waSRW);
    WakeAllConditionVariable(&g_waCV);
    ReleaseSRWLockExclusive(&g_waSRW);
}

// ============================================================
// WakeByAddressSingle - L0
// ============================================================
COMPAT_API VOID WINAPI WakeByAddressSingle(PVOID Address)
{
    (void)Address;
    Compat_EnsureWaCV();
    AcquireSRWLockExclusive(&g_waSRW);
    WakeConditionVariable(&g_waCV);
    ReleaseSRWLockExclusive(&g_waSRW);
}

// ============================================================
// InitOnceExecuteOnce - L1
// Introduced: Windows Vista
// ============================================================
COMPAT_API BOOL WINAPI InitOnceExecuteOnce(PINIT_ONCE InitOnce, PINIT_ONCE_FN InitFn, PVOID Parameter, LPVOID *Context)
{
    typedef BOOL(WINAPI* InitOnceExecuteOnceFn)(PINIT_ONCE, PINIT_ONCE_FN, PVOID, LPVOID*);
    auto realFn = (InitOnceExecuteOnceFn)Compat_GetRealProc("kernel32", "InitOnceExecuteOnce");
    if (realFn) return realFn(InitOnce, InitFn, Parameter, Context);

    if (InitOnce->Ptr == NULL) {
        InitOnce->Ptr = (PVOID)1;
        BOOL r = InitFn ? InitFn(InitOnce, Parameter, Context) : TRUE;
        InitOnce->Ptr = (PVOID)2;
        return r;
    } else if (InitOnce->Ptr == (PVOID)1) {
        volatile LONG* p = (LONG*)&InitOnce->Ptr;
        while (*p == 1) Sleep(1);
        return TRUE;
    } else {
        return TRUE;
    }
}
