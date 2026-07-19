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
