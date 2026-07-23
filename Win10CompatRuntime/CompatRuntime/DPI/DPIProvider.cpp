// DPIProvider.cpp - DPI API compatibility implementations.
// GetDpiForWindow -> L1 (fallback to GetDeviceCaps)
// SetProcessDpiAwarenessContext -> L3 (stub, return TRUE)
// EnableNonClientDpiScaling -> L3 (stub, return TRUE)
// SetProcessDPIAware -> already on Win7, just forward
// GetDpiForSystem -> L1 (fallback to GetDeviceCaps)
// GetDpiForMonitor -> L1 (fallback)

#include "../CompatRuntime.h"
#include <ShellScalingApi.h>

// ============================================================
// GetDpiForWindow - L1 (fallback to GetDeviceCaps)
// Introduced: Win10 1607 (build 14393)
// ============================================================
COMPAT_API UINT WINAPI GetDpiForWindow(HWND hwnd)
{
    HDC hdc = GetDC(hwnd);
    if (!hdc) return 96;
    UINT dpi = (UINT)GetDeviceCaps(hdc, LOGPIXELSX);
    ReleaseDC(hwnd, hdc);
    return dpi ? dpi : 96;
}

// ============================================================
// GetDpiForSystem - L1 (fallback)
// Introduced: Win10 1607 (build 14393)
// ============================================================
COMPAT_API UINT WINAPI GetDpiForSystem()
{
    HDC hdc = GetDC(NULL);
    if (!hdc) return 96;
    UINT dpi = (UINT)GetDeviceCaps(hdc, LOGPIXELSX);
    ReleaseDC(NULL, hdc);
    return dpi ? dpi : 96;
}

// ============================================================
// SetProcessDpiAwarenessContext - L3 (stub)
// Introduced: Win10 1703 (build 15063)
// ============================================================
COMPAT_API BOOL WINAPI SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT value)
{
    (void)value;
    // Just return TRUE; the system may or may not support DPI awareness.
    return TRUE;
}

// ============================================================
// EnableNonClientDpiScaling - L3 (stub)
// Introduced: Win10 1607 (build 14393)
// ============================================================
COMPAT_API BOOL WINAPI EnableNonClientDpiScaling(HWND hwnd)
{
    (void)hwnd;
    return TRUE;
}

// ============================================================
// GetDpiForMonitor - L1 (forward if present, else fallback 96)
// Introduced: Win8.1 / Win10
// ============================================================
COMPAT_API HRESULT WINAPI GetDpiForMonitor(HMONITOR hmonitor, MONITOR_DPI_TYPE dpiType, UINT* dpiX, UINT* dpiY)
{
    typedef HRESULT(WINAPI* GetDpiForMonitorPtr)(HMONITOR, MONITOR_DPI_TYPE, UINT*, UINT*);
    auto realFn = (GetDpiForMonitorPtr)Compat_GetRealProc("user32", "GetDpiForMonitor");
    if (realFn)
    {
        return realFn(hmonitor, dpiType, dpiX, dpiY);
    }
    if (dpiX) *dpiX = 96;
    if (dpiY) *dpiY = 96;
    return S_OK;
}

// ============================================================
// SetProcessDpiAwareness - L1 (forward if present, else fallback)
// Introduced: Win8.1
// ============================================================
COMPAT_API HRESULT WINAPI SetProcessDpiAwareness(PROCESS_DPI_AWARENESS value)
{
    typedef HRESULT(WINAPI* SetProcessDpiAwarenessPtr)(PROCESS_DPI_AWARENESS);
    auto realFn = (SetProcessDpiAwarenessPtr)Compat_GetRealProc("user32", "SetProcessDpiAwareness");
    if (realFn)
    {
        return realFn(value);
    }
    if (value >= PROCESS_PER_MONITOR_DPI_AWARE)
    {
        SetProcessDPIAware();
    }
    return S_OK;
}

// ============================================================
// GetProcessDpiAwareness - L1 (forward if present, else fallback)
// Introduced: Win8.1
// ============================================================
COMPAT_API HRESULT WINAPI GetProcessDpiAwareness(HANDLE hprocess, PROCESS_DPI_AWARENESS* value)
{
    typedef HRESULT(WINAPI* GetProcessDpiAwarenessPtr)(HANDLE, PROCESS_DPI_AWARENESS*);
    auto realFn = (GetProcessDpiAwarenessPtr)Compat_GetRealProc("user32", "GetProcessDpiAwareness");
    if (realFn)
    {
        return realFn(hprocess, value);
    }
    if (value) *value = PROCESS_SYSTEM_DPI_AWARE;
    return S_OK;
}

// ============================================================
// AreDpiAwarenessContextsEqual - L1 (forward if present, else fallback)
// Introduced: Win10 1607 (build 14393)
// ============================================================
COMPAT_API BOOL WINAPI AreDpiAwarenessContextsEqual(DPI_AWARENESS_CONTEXT dpiContextA, DPI_AWARENESS_CONTEXT dpiContextB)
{
    typedef BOOL(WINAPI* AreDpiAwarenessContextsEqualPtr)(DPI_AWARENESS_CONTEXT, DPI_AWARENESS_CONTEXT);
    auto realFn = (AreDpiAwarenessContextsEqualPtr)Compat_GetRealProc("user32", "AreDpiAwarenessContextsEqual");
    if (realFn)
    {
        return realFn(dpiContextA, dpiContextB);
    }
    return (dpiContextA == dpiContextB);
}

// ============================================================
// GetProcessDpiAwarenessContext - L1 (forward if present, else fallback)
// Introduced: Win10 1607 (build 14393)
// ============================================================
COMPAT_API DPI_AWARENESS_CONTEXT WINAPI GetProcessDpiAwarenessContext(VOID)
{
    typedef DPI_AWARENESS_CONTEXT(WINAPI* GetProcessDpiAwarenessContextPtr)(VOID);
    auto realFn = (GetProcessDpiAwarenessContextPtr)Compat_GetRealProc("user32", "GetProcessDpiAwarenessContext");
    if (realFn)
    {
        return realFn();
    }
    return DPI_AWARENESS_CONTEXT_SYSTEM_AWARE;
}

// ============================================================
// GetThreadDpiAwarenessContext - L1 (forward if present, else fallback)
// Introduced: Win10 1607 (build 14393)
// ============================================================
COMPAT_API DPI_AWARENESS_CONTEXT WINAPI GetThreadDpiAwarenessContext(VOID)
{
    typedef DPI_AWARENESS_CONTEXT(WINAPI* GetThreadDpiAwarenessContextPtr)(VOID);
    auto realFn = (GetThreadDpiAwarenessContextPtr)Compat_GetRealProc("user32", "GetThreadDpiAwarenessContext");
    if (realFn)
    {
        return realFn();
    }
    return DPI_AWARENESS_CONTEXT_SYSTEM_AWARE;
}

// ============================================================
// SetThreadDpiAwarenessContext - L1 (forward if present, else fallback)
// Introduced: Win10 1607 (build 14393)
// ============================================================
COMPAT_API DPI_AWARENESS_CONTEXT WINAPI SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT dpiContext)
{
    typedef DPI_AWARENESS_CONTEXT(WINAPI* SetThreadDpiAwarenessContextPtr)(DPI_AWARENESS_CONTEXT);
    auto realFn = (SetThreadDpiAwarenessContextPtr)Compat_GetRealProc("user32", "SetThreadDpiAwarenessContext");
    if (realFn)
    {
        return realFn(dpiContext);
    }
    (void)dpiContext;
    return dpiContext;
}

// ============================================================
// GetAwarenessFromDpiAwarenessContext - L1 (forward if present, else fallback)
// Introduced: Win10 1607 (build 14393)
// ============================================================
COMPAT_API DPI_AWARENESS WINAPI GetAwarenessFromDpiAwarenessContext(DPI_AWARENESS_CONTEXT dpiContext)
{
    typedef DPI_AWARENESS(WINAPI* GetAwarenessFromDpiAwarenessContextPtr)(DPI_AWARENESS_CONTEXT);
    auto realFn = (GetAwarenessFromDpiAwarenessContextPtr)Compat_GetRealProc("user32", "GetAwarenessFromDpiAwarenessContext");
    if (realFn)
    {
        return realFn(dpiContext);
    }
    (void)dpiContext;
    return DPI_AWARENESS_SYSTEM_AWARE;
}

// ============================================================
// EnableChildWindowDpiMessages - L1 (forward if present, else fallback)
// Introduced: Win10 1607 (build 14393)
// ============================================================
COMPAT_API BOOL WINAPI EnableChildWindowDpiMessages(HWND hwnd, BOOL bEnable)
{
    typedef BOOL(WINAPI* EnableChildWindowDpiMessagesPtr)(HWND, BOOL);
    auto realFn = (EnableChildWindowDpiMessagesPtr)Compat_GetRealProc("user32", "EnableChildWindowDpiMessages");
    if (realFn)
    {
        return realFn(hwnd, bEnable);
    }
    (void)hwnd;
    (void)bEnable;
    return TRUE;
}
