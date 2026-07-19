// TimeProvider.cpp - Time API compatibility implementations.
// GetSystemTimePreciseAsFileTime -> L1 (fallback to GetSystemTimeAsFileTime)
// QueryInterruptTimePrecise -> L1 (fallback to GetTickCount64 * 10000)
// GetTickCount64 -> L1 (fallback to GetTickCount with extension)

#include "../CompatRuntime.h"

// ============================================================
// GetSystemTimePreciseAsFileTime - L1 (degrade to GetSystemTimeAsFileTime)
// Introduced: Win10 1607 (build 14393)
// ============================================================
COMPAT_API VOID WINAPI GetSystemTimePreciseAsFileTime(LPFILETIME lpSystemTimeAsFileTime)
{
    // The only difference is precision: the "Precise" version uses QPC
    // to sub-microsecond accuracy. GetSystemTimeAsFileTime is accurate
    // to ~15.6ms (the system timer tick). For most callers this is fine.
    GetSystemTimeAsFileTime(lpSystemTimeAsFileTime);
}

// ============================================================
// QueryInterruptTimePrecise - L1 (approximate via GetTickCount64)
// Introduced: Win10 1803 (build 17134)
// ============================================================
COMPAT_API VOID WINAPI QueryInterruptTimePrecise(PULONGLONG lpInterruptTimePrecise)
{
    if (!lpInterruptTimePrecise) return;
    // QueryInterruptTimePrecise returns time in 100ns units since boot
    // (excluding sleep). GetTickCount64 returns milliseconds since boot.
    *lpInterruptTimePrecise = GetTickCount64() * 10000ULL;
}

// ============================================================
// QueryUnbiasedInterruptTimePrecise - L1 (approximate)
// Introduced: Win10 1803 (build 17134)
// ============================================================
COMPAT_API VOID WINAPI QueryUnbiasedInterruptTimePrecise(PULONGLONG lpUnbiasedInterruptTimePrecise)
{
    if (!lpUnbiasedInterruptTimePrecise) return;
    // QueryUnbiasedInterruptTimePrecise excludes time spent in connected
    // standby (modern sleep). GetTickCount64 already excludes this on
    // most systems. Approximate with GetTickCount64.
    *lpUnbiasedInterruptTimePrecise = GetTickCount64() * 10000ULL;
}

// ============================================================
// GetTickCount64 - L1 (fallback to GetTickCount with high-dword extension)
// Introduced: Win10 1607 (but actually available since Vista via API-MS)
// ============================================================
COMPAT_API ULONGLONG WINAPI GetTickCount64()
{
    // On modern Windows this is already exported. If not, use GetTickCount.
    typedef ULONGLONG (WINAPI *GetTickCount64_t)(void);
    static GetTickCount64_t pFunc = NULL;
    if (!pFunc)
    {
        pFunc = (GetTickCount64_t)GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetTickCount64");
    }
    if (pFunc) return pFunc();
    // Fallback: GetTickCount wraps at ~49.7 days.
    return (ULONGLONG)GetTickCount();
}
