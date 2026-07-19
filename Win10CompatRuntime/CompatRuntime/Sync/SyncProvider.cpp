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
    ReleaseSRWLockExclusive(SRWLock);
    DWORD result = WaitForSingleObject(cv->hEvent, dwMilliseconds);
    AcquireSRWLockExclusive(SRWLock);
    if (result == WAIT_OBJECT_0) return TRUE;
    SetLastError(result == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError());
    return FALSE;
}
