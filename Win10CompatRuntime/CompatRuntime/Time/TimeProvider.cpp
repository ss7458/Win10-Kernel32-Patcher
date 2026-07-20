// TimeProvider.cpp - Time API compatibility implementations.
// Strategy: L1 forward to real OS implementation when present, else fallback
// to older equivalent API.

#include "../CompatRuntime.h"

// ============================================================
// GetSystemTimePreciseAsFileTime - L1
// Introduced: Win8 (build 9200)
// Forward to real OS implementation when present; on older
// systems without the API, derive a high-precision time by
// interpolating system time with QueryPerformanceCounter.
// ============================================================
COMPAT_API VOID WINAPI GetSystemTimePreciseAsFileTime(LPFILETIME lpSystemTimeAsFileTime)
{
    typedef VOID(WINAPI* PFN)(LPFILETIME);
    static PFN pfn = nullptr;
    if (!pfn) pfn = (PFN)Compat_GetRealProc("kernel32", "GetSystemTimePreciseAsFileTime");
    if (pfn) { pfn(lpSystemTimeAsFileTime); return; }

    // Fallback: interpolate system time using QPC for sub-millisecond precision.
    static LARGE_INTEGER s_freq = { 0 };
    static LARGE_INTEGER s_refQpc = { 0 };
    static ULARGE_INTEGER s_refSys = { 0 };
    static LONGLONG s_lastAnchor = 0;

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);

    if (s_freq.QuadPart == 0 || (now.QuadPart - s_lastAnchor) > s_freq.QuadPart)
    {
        if (s_freq.QuadPart == 0) QueryPerformanceFrequency(&s_freq);
        s_refQpc = now;
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        s_refSys.LowPart = ft.dwLowDateTime;
        s_refSys.HighPart = ft.dwHighDateTime;
        s_lastAnchor = now.QuadPart;
    }

    LONGLONG delta = now.QuadPart - s_refQpc.QuadPart;
    LONGLONG delta100ns = (delta * 10000000) / s_freq.QuadPart;
    ULARGE_INTEGER result;
    result.QuadPart = s_refSys.QuadPart + delta100ns;
    lpSystemTimeAsFileTime->dwLowDateTime = result.LowPart;
    lpSystemTimeAsFileTime->dwHighDateTime = result.HighPart;
}

// ============================================================
// GetSystemTimePrecise - L1
// Introduced: Win8 (build 9200)
// ============================================================
COMPAT_API VOID WINAPI GetSystemTimePrecise(LPSYSTEMTIME lpSystemTime)
{
    typedef VOID(WINAPI* PFN)(LPSYSTEMTIME);
    static PFN pfn = nullptr;
    if (!pfn) pfn = (PFN)Compat_GetRealProc("kernel32", "GetSystemTimePrecise");
    if (pfn) { pfn(lpSystemTime); return; }
    GetSystemTime(lpSystemTime);
}

// ============================================================
// QueryInterruptTime - L1
// Introduced: Win10 1507 (build 10240)
// Derives from QueryPerformanceCounter when real API absent.
// ============================================================
COMPAT_API VOID WINAPI QueryInterruptTime(PULONG64 lpInterruptTime)
{
    typedef VOID(WINAPI* PFN)(PULONG64);
    static PFN pfn = nullptr;
    if (!pfn) pfn = (PFN)Compat_GetRealProc("kernel32", "QueryInterruptTime");
    if (pfn) { pfn(lpInterruptTime); return; }
    if (!lpInterruptTime) return;
    LARGE_INTEGER count, freq;
    if (QueryPerformanceCounter(&count) && QueryPerformanceFrequency(&freq) && freq.QuadPart != 0)
    {
        *lpInterruptTime = (ULONG64)((double)count.QuadPart / (double)freq.QuadPart * 10000000.0);
    }
    else
    {
        *lpInterruptTime = 0;
    }
}

// ============================================================
// QueryUnbiasedInterruptTime - L1
// Introduced: Win10 1507 (build 10240)
// Same derivation as QueryInterruptTime (ignores bias).
// ============================================================
COMPAT_API BOOL WINAPI QueryUnbiasedInterruptTime(PULONG64 lpUnbiasedInterruptTime)
{
    typedef BOOL(WINAPI* PFN)(PULONG64);
    static PFN pfn = nullptr;
    if (!pfn) pfn = (PFN)Compat_GetRealProc("kernel32", "QueryUnbiasedInterruptTime");
    if (pfn) return pfn(lpUnbiasedInterruptTime);
    if (!lpUnbiasedInterruptTime) return FALSE;
    LARGE_INTEGER count, freq;
    if (QueryPerformanceCounter(&count) && QueryPerformanceFrequency(&freq) && freq.QuadPart != 0)
    {
        *lpUnbiasedInterruptTime = (ULONG64)((double)count.QuadPart / (double)freq.QuadPart * 10000000.0);
        return TRUE;
    }
    else
    {
        *lpUnbiasedInterruptTime = 0;
        return FALSE;
    }
}

// ============================================================
// QueryInterruptTimePrecise - L1
// Introduced: Win10 1507 (build 10240)
// Same derivation as QueryInterruptTime.
// ============================================================
COMPAT_API VOID WINAPI QueryInterruptTimePrecise(PULONG64 lpInterruptTimePrecise)
{
    typedef VOID(WINAPI* PFN)(PULONG64);
    static PFN pfn = nullptr;
    if (!pfn) pfn = (PFN)Compat_GetRealProc("kernel32", "QueryInterruptTimePrecise");
    if (pfn) { pfn(lpInterruptTimePrecise); return; }
    if (!lpInterruptTimePrecise) return;
    LARGE_INTEGER count, freq;
    if (QueryPerformanceCounter(&count) && QueryPerformanceFrequency(&freq) && freq.QuadPart != 0)
    {
        *lpInterruptTimePrecise = (ULONG64)((double)count.QuadPart / (double)freq.QuadPart * 10000000.0);
    }
    else
    {
        *lpInterruptTimePrecise = 0;
    }
}

// ============================================================
// QueryUnbiasedInterruptTimePrecise - L1
// Introduced: Win10 1507 (build 10240)
// Same derivation as QueryInterruptTime.
// ============================================================
COMPAT_API VOID WINAPI QueryUnbiasedInterruptTimePrecise(PULONG64 lpUnbiasedInterruptTimePrecise)
{
    typedef VOID(WINAPI* PFN)(PULONG64);
    static PFN pfn = nullptr;
    if (!pfn) pfn = (PFN)Compat_GetRealProc("kernel32", "QueryUnbiasedInterruptTimePrecise");
    if (pfn) { pfn(lpUnbiasedInterruptTimePrecise); return; }
    if (!lpUnbiasedInterruptTimePrecise) return;
    LARGE_INTEGER count, freq;
    if (QueryPerformanceCounter(&count) && QueryPerformanceFrequency(&freq) && freq.QuadPart != 0)
    {
        *lpUnbiasedInterruptTimePrecise = (ULONG64)((double)count.QuadPart / (double)freq.QuadPart * 10000000.0);
    }
    else
    {
        *lpUnbiasedInterruptTimePrecise = 0;
    }
}

// ============================================================
// GetTickCount64 - L1
// Introduced: Vista (build 6000)
// ============================================================
COMPAT_API ULONGLONG WINAPI GetTickCount64(VOID)
{
    typedef ULONGLONG(WINAPI* PFN)(VOID);
    static PFN pfn = nullptr;
    if (!pfn) pfn = (PFN)Compat_GetRealProc("kernel32", "GetTickCount64");
    if (pfn) { return pfn(); }
    return (ULONGLONG)GetTickCount();
}
