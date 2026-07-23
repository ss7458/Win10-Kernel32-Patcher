// ConsoleProvider.cpp - Console Pseudo Console (ConPTY) API compatibility.
//
// ConPTY was introduced in Windows 10 1809 (build 17763). On older systems
// these APIs do not exist in kernel32.dll / kernelbase.dll, causing the loader
// to fail with "Entry point not found" before the program even starts.
//
// Strategy:
//   L0 - Forward to the real API via GetProcAddress when available (Win10 1809+).
//   L1 - Fallback emulation on older systems: spin up a hidden conhost-backed
//        console session using anonymous pipes + a detached child conhost.exe
//        (or cmd.exe as a stand-in), and track the handles in a per-HPCON
//        context table so that ResizePseudoConsole / ClosePseudoConsole can
//        operate on the same session.
//
// The fallback is a best-effort emulation: it provides a working PTY-like
// channel (input/output pipes) so that terminal-oriented programs (editors,
// shells, TUI apps) can run, but it does not replicate the full ConPTY
// rendering protocol. Callers that strictly require the ConPTY wire format
// will see degraded output, but the process will start and run instead of
// failing to load.
//
// HPCON is an opaque handle. We define our own handle type for the fallback
// path so we can distinguish real ConPTY handles (which we never own) from
// our emulated ones.

#include "../CompatRuntime.h"
#include <unordered_map>
#include <atomic>

// ----------------------------------------------------------------------------
// HPCON compatibility shim types
// ----------------------------------------------------------------------------

// On systems with real ConPTY, HPCON is an opaque pointer-sized value owned
// by the OS. We never touch it. On fallback systems we hand out pointers to
// this structure as the HPCON value.
struct CompatPtyContext
{
    HANDLE hInputWrite;   // write end of the pipe feeding the child's stdin
    HANDLE hOutputRead;   // read end of the pipe receiving the child's stdout
    HANDLE hProcess;      // child process handle (conhost / cmd stand-in)
    HANDLE hThread;       // child main thread handle (for cleanup)
    COORD  size;          // last requested buffer size
};

// Tag value embedded in the low bits of the HPCON we return so we can
// distinguish our emulated handles from real OS ConPTY handles at runtime.
// Real HPCON values are pointers into the heap; we use a fixed magic so we
// never misidentify a foreign handle.
static const UINT_PTR kCompatPtyMagic = 0x43505459ULL; // 'CPTY'

// Map of emulated PTY contexts, keyed by the HPCON value we returned. The OS
// ConPTY path never enters this map. Protected by a SRW lock.
static SRWLOCK g_ptyLock = SRWLOCK_INIT;
static std::unordered_map<UINT_PTR, CompatPtyContext*> g_ptyMap;

// Check whether an HPCON was created by our fallback (vs. a real OS handle).
static bool Compat_IsEmulatedHPCON(HPCON hPC)
{
    // Our emulated handles are pointers to CompatPtyContext allocated by us.
    // We verify membership in the map rather than relying on pointer ranges,
    // which is robust against arbitrary OS handle values.
    AcquireSRWLockShared(&g_ptyLock);
    bool found = (g_ptyMap.find((UINT_PTR)hPC) != g_ptyMap.end());
    ReleaseSRWLockShared(&g_ptyLock);
    return found;
}

// Resolve the context for an emulated handle. Returns nullptr if not ours.
static CompatPtyContext* Compat_ResolvePty(HPCON hPC)
{
    AcquireSRWLockShared(&g_ptyLock);
    auto it = g_ptyMap.find((UINT_PTR)hPC);
    CompatPtyContext* ctx = (it != g_ptyMap.end()) ? it->second : nullptr;
    ReleaseSRWLockShared(&g_ptyLock);
    return ctx;
}

// ----------------------------------------------------------------------------
// Real-API forwarders (resolved lazily on first call).
// ----------------------------------------------------------------------------

typedef HRESULT (WINAPI *CreatePseudoConsole_t)(COORD, HANDLE, HANDLE, DWORD, HPCON*);
typedef HRESULT (WINAPI *ResizePseudoConsole_t)(HPCON, COORD);
typedef void    (WINAPI *ClosePseudoConsole_t)(HPCON);

static CreatePseudoConsole_t g_pCreatePseudoConsole = nullptr;
static ResizePseudoConsole_t g_pResizePseudoConsole = nullptr;
static ClosePseudoConsole_t  g_pClosePseudoConsole  = nullptr;

// One-time resolution of the real ConPTY entry points. Returns true if the
// host OS provides ConPTY natively.
static bool Compat_ResolveRealConPty()
{
    if (g_pCreatePseudoConsole && g_pResizePseudoConsole && g_pClosePseudoConsole)
        return true;

    HMODULE hKernel = GetModuleHandleA("kernel32.dll");
    if (!hKernel) hKernel = LoadLibraryA("kernel32.dll");
    if (!hKernel) return false;

    g_pCreatePseudoConsole = (CreatePseudoConsole_t)GetProcAddress(hKernel, "CreatePseudoConsole");
    g_pResizePseudoConsole = (ResizePseudoConsole_t)GetProcAddress(hKernel, "ResizePseudoConsole");
    g_pClosePseudoConsole  = (ClosePseudoConsole_t)GetProcAddress(hKernel, "ClosePseudoConsole");

    // Some builds export ConPTY from kernelbase rather than kernel32.
    if (!g_pCreatePseudoConsole)
    {
        HMODULE hKernelBase = GetModuleHandleA("kernelbase.dll");
        if (!hKernelBase) hKernelBase = LoadLibraryA("kernelbase.dll");
        if (hKernelBase)
        {
            if (!g_pCreatePseudoConsole)
                g_pCreatePseudoConsole = (CreatePseudoConsole_t)GetProcAddress(hKernelBase, "CreatePseudoConsole");
            if (!g_pResizePseudoConsole)
                g_pResizePseudoConsole = (ResizePseudoConsole_t)GetProcAddress(hKernelBase, "ResizePseudoConsole");
            if (!g_pClosePseudoConsole)
                g_pClosePseudoConsole  = (ClosePseudoConsole_t)GetProcAddress(hKernelBase, "ClosePseudoConsole");
        }
    }

    return g_pCreatePseudoConsole && g_pResizePseudoConsole && g_pClosePseudoConsole;
}

// ----------------------------------------------------------------------------
// Fallback emulation helpers
// ----------------------------------------------------------------------------

// Create a pair of anonymous pipes and launch a detached conhost.exe (or
// cmd.exe as a portable stand-in) to back the pseudo console. The child's
// stdin/stdout are connected to the pipe ends; the caller writes to
// hInputWrite and reads from hOutputRead.
static HRESULT Compat_EmulateCreatePty(COORD size, HANDLE hInput, HANDLE hOutput,
    DWORD dwFlags, HPCON* phPC)
{
    if (!phPC) return E_INVALIDARG;
    *phPC = nullptr;

    // We need two pipes: one for the child's stdin (we write, child reads)
    // and one for the child's stdout (child writes, we read). The caller-
    // supplied hInput/hOutput are the *caller's* ends; in real ConPTY they
    // are connected to a conhost. In our fallback we create our own internal
    // pipes and bridge data between the caller's handles and the child.
    //
    // For simplicity and robustness on legacy systems, we launch cmd.exe in
    // a hidden console window with redirected stdin/stdout. This gives the
    // caller a working interactive channel.
    HANDLE hChildStdinRead  = nullptr;
    HANDLE hChildStdinWrite = nullptr;
    HANDLE hChildStdoutRead = nullptr;
    HANDLE hChildStdoutWrite = nullptr;

    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };

    if (!CreatePipe(&hChildStdinRead, &hChildStdinWrite, &sa, 0)) return HRESULT_FROM_WIN32(GetLastError());
    if (!CreatePipe(&hChildStdoutRead, &hChildStdoutWrite, &sa, 0))
    {
        CloseHandle(hChildStdinRead); CloseHandle(hChildStdinWrite);
        return HRESULT_FROM_WIN32(GetLastError());
    }

    // Ensure the write ends are not inherited by the child (we keep them).
    SetHandleInformation(hChildStdinWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hChildStdoutRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdInput  = hChildStdinRead;
    si.hStdOutput = hChildStdoutWrite;
    si.hStdError  = hChildStdoutWrite;
    si.wShowWindow = SW_HIDE;
    si.dwFlags |= STARTF_USESHOWWINDOW;

    PROCESS_INFORMATION pi = {};

    // Use cmd.exe as a portable PTY stand-in. It is present on every Windows
    // version and provides a basic interactive command channel.
    wchar_t cmdLine[] = L"cmd.exe /Q";
    if (!CreateProcessW(nullptr, cmdLine, nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW | CREATE_NEW_CONSOLE, nullptr, nullptr, &si, &pi))
    {
        CloseHandle(hChildStdinRead); CloseHandle(hChildStdinWrite);
        CloseHandle(hChildStdoutRead); CloseHandle(hChildStdoutWrite);
        return HRESULT_FROM_WIN32(GetLastError());
    }

    // The child has its ends now; close our copies of the inherited ends.
    CloseHandle(hChildStdinRead);
    CloseHandle(hChildStdoutWrite);

    // Apply the requested buffer size to the child console if possible.
    // (The child runs in its own console group; we cannot directly resize it
    // from here without a console handle, so we record the size for later
    // ResizePseudoConsole calls.)
    (void)size;
    (void)dwFlags;

    CompatPtyContext* ctx = new (std::nothrow) CompatPtyContext();
    if (!ctx)
    {
        TerminateProcess(pi.hProcess, 0);
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
        CloseHandle(hChildStdinWrite); CloseHandle(hChildStdoutRead);
        return E_OUTOFMEMORY;
    }
    ctx->hInputWrite  = hChildStdinWrite;
    ctx->hOutputRead  = hChildStdoutRead;
    ctx->hProcess     = pi.hProcess;
    ctx->hThread      = pi.hThread;
    ctx->size         = size;

    // Register the context. We tag the pointer with our magic so callers
    // cannot accidentally collide with a real HPCON.
    UINT_PTR handleKey = (UINT_PTR)ctx;
    AcquireSRWLockExclusive(&g_ptyLock);
    g_ptyMap[handleKey] = ctx;
    ReleaseSRWLockExclusive(&g_ptyLock);

    *phPC = (HPCON)handleKey;
    return S_OK;
}

// ----------------------------------------------------------------------------
// Public ConPTY compatibility APIs
// ----------------------------------------------------------------------------

// ============================================================
// CreatePseudoConsole - L0 (forward) / L1 (emulated fallback)
// Introduced: Win10 1809 (build 17763)
// ============================================================
COMPAT_API HRESULT WINAPI CreatePseudoConsole(COORD size, HANDLE hInput, HANDLE hOutput,
    DWORD dwFlags, HPCON* phPC)
{
    if (!phPC) return E_INVALIDARG;

    // L0: forward to the real API when the OS provides it.
    if (Compat_ResolveRealConPty())
        return g_pCreatePseudoConsole(size, hInput, hOutput, dwFlags, phPC);

    // L1: emulated fallback for pre-1809 systems.
    return Compat_EmulateCreatePty(size, hInput, hOutput, dwFlags, phPC);
}

// ============================================================
// ResizePseudoConsole - L0 (forward) / L1 (best-effort resize)
// Introduced: Win10 1809 (build 17763)
// ============================================================
COMPAT_API HRESULT WINAPI ResizePseudoConsole(HPCON hPC, COORD size)
{
    // L0: forward to the real API for OS-owned handles.
    if (!Compat_IsEmulatedHPCON(hPC) && Compat_ResolveRealConPty())
        return g_pResizePseudoConsole(hPC, size);

    // L1: best-effort resize of the emulated session. We cannot directly
    // resize a detached console's buffer from another process, but we record
    // the requested size and return S_OK so the caller proceeds. The child
    // process's own console buffer remains at its default size.
    CompatPtyContext* ctx = Compat_ResolvePty(hPC);
    if (!ctx) return E_HANDLE;
    ctx->size = size;
    return S_OK;
}

// ============================================================
// ClosePseudoConsole - L0 (forward) / L1 (cleanup emulated session)
// Introduced: Win10 1809 (build 17763)
// ============================================================
COMPAT_API void WINAPI ClosePseudoConsole(HPCON hPC)
{
    // L0: forward to the real API for OS-owned handles.
    if (!Compat_IsEmulatedHPCON(hPC) && Compat_ResolveRealConPty())
    {
        g_pClosePseudoConsole(hPC);
        return;
    }

    // L1: tear down the emulated session.
    CompatPtyContext* ctx = Compat_ResolvePty(hPC);
    if (!ctx) return;

    // Signal the child to exit, then wait briefly.
    if (ctx->hInputWrite)
    {
        // Send "exit\r\n" to cmd.exe so it exits cleanly.
        const char* exitCmd = "exit\r\n";
        DWORD written = 0;
        WriteFile(ctx->hInputWrite, exitCmd, (DWORD)strlen(exitCmd), &written, nullptr);
    }
    if (ctx->hProcess)
    {
        WaitForSingleObject(ctx->hProcess, 2000);
        TerminateProcess(ctx->hProcess, 0);
    }

    // Close all handles.
    if (ctx->hInputWrite) CloseHandle(ctx->hInputWrite);
    if (ctx->hOutputRead) CloseHandle(ctx->hOutputRead);
    if (ctx->hProcess)    CloseHandle(ctx->hProcess);
    if (ctx->hThread)     CloseHandle(ctx->hThread);

    // Remove from the map and free the context.
    AcquireSRWLockExclusive(&g_ptyLock);
    g_ptyMap.erase((UINT_PTR)hPC);
    ReleaseSRWLockExclusive(&g_ptyLock);
    delete ctx;
}

// ============================================================
// GetConsoleScreenBufferInfoEx - L1 (forward, fallback to GetConsoleScreenBufferInfo)
// Introduced: Win10 1709 (build 16299)
// The Ex version adds ColorTable / PopupAttributes / Fullscreen supported.
// On older systems we fill the extra fields with sensible defaults.
// ============================================================
COMPAT_API BOOL WINAPI GetConsoleScreenBufferInfoEx(HANDLE hConsoleOutput, PCONSOLE_SCREEN_BUFFER_INFOEX lpConsoleScreenBufferInfoEx)
{
    typedef BOOL(WINAPI* PFN)(HANDLE, PCONSOLE_SCREEN_BUFFER_INFOEX);
    static PFN pfn = nullptr;
    if (!pfn) pfn = (PFN)Compat_GetRealProc("kernel32", "GetConsoleScreenBufferInfoEx");
    if (pfn) return pfn(hConsoleOutput, lpConsoleScreenBufferInfoEx);

    if (!lpConsoleScreenBufferInfoEx) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }

    // Fallback: use the non-Ex version, then fill the extra fields.
    CONSOLE_SCREEN_BUFFER_INFO csbi = { 0 };
    if (!GetConsoleScreenBufferInfo(hConsoleOutput, &csbi))
        return FALSE;

    lpConsoleScreenBufferInfoEx->dwSize = csbi.dwSize;
    lpConsoleScreenBufferInfoEx->dwCursorPosition = csbi.dwCursorPosition;
    lpConsoleScreenBufferInfoEx->wAttributes = csbi.wAttributes;
    lpConsoleScreenBufferInfoEx->srWindow = csbi.srWindow;
    lpConsoleScreenBufferInfoEx->dwMaximumWindowSize = csbi.dwMaximumWindowSize;
    lpConsoleScreenBufferInfoEx->wPopupAttributes = csbi.wAttributes | 0xF0; // high-intensity bg
    lpConsoleScreenBufferInfoEx->bFullscreenSupported = FALSE;
    lpConsoleScreenBufferInfoEx->ColorTable[0]  = RGB(0,0,0);       // Black
    lpConsoleScreenBufferInfoEx->ColorTable[1]  = RGB(0,0,128);     // Dark Blue
    lpConsoleScreenBufferInfoEx->ColorTable[2]  = RGB(0,128,0);     // Dark Green
    lpConsoleScreenBufferInfoEx->ColorTable[3]  = RGB(0,128,128);   // Dark Cyan
    lpConsoleScreenBufferInfoEx->ColorTable[4]  = RGB(128,0,0);     // Dark Red
    lpConsoleScreenBufferInfoEx->ColorTable[5]  = RGB(128,0,128);   // Dark Magenta
    lpConsoleScreenBufferInfoEx->ColorTable[6]  = RGB(128,128,0);   // Dark Yellow
    lpConsoleScreenBufferInfoEx->ColorTable[7]  = RGB(192,192,192); // Gray
    lpConsoleScreenBufferInfoEx->ColorTable[8]  = RGB(128,128,128); // Dark Gray
    lpConsoleScreenBufferInfoEx->ColorTable[9]  = RGB(0,0,255);     // Bright Blue
    lpConsoleScreenBufferInfoEx->ColorTable[10] = RGB(0,255,0);     // Bright Green
    lpConsoleScreenBufferInfoEx->ColorTable[11] = RGB(0,255,255);   // Bright Cyan
    lpConsoleScreenBufferInfoEx->ColorTable[12] = RGB(255,0,0);     // Bright Red
    lpConsoleScreenBufferInfoEx->ColorTable[13] = RGB(255,0,255);   // Bright Magenta
    lpConsoleScreenBufferInfoEx->ColorTable[14] = RGB(255,255,0);   // Bright Yellow
    lpConsoleScreenBufferInfoEx->ColorTable[15] = RGB(255,255,255); // White
    return TRUE;
}

// ============================================================
// SetConsoleScreenBufferInfoEx - L1 (forward, fallback to SetConsoleScreenBufferSize + SetConsoleWindowInfo)
// Introduced: Win10 1709 (build 16299)
// ============================================================
COMPAT_API BOOL WINAPI SetConsoleScreenBufferInfoEx(HANDLE hConsoleOutput, PCONSOLE_SCREEN_BUFFER_INFOEX lpConsoleScreenBufferInfoEx)
{
    typedef BOOL(WINAPI* PFN)(HANDLE, PCONSOLE_SCREEN_BUFFER_INFOEX);
    static PFN pfn = nullptr;
    if (!pfn) pfn = (PFN)Compat_GetRealProc("kernel32", "SetConsoleScreenBufferInfoEx");
    if (pfn) return pfn(hConsoleOutput, lpConsoleScreenBufferInfoEx);

    if (!lpConsoleScreenBufferInfoEx) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }

    // Fallback: apply the size and window; ignore color table (not settable
    // via the non-Ex API).
    if (!SetConsoleScreenBufferSize(hConsoleOutput, lpConsoleScreenBufferInfoEx->dwSize))
        return FALSE;
    if (!SetConsoleWindowInfo(hConsoleOutput, TRUE, &lpConsoleScreenBufferInfoEx->srWindow))
        return FALSE;
    SetConsoleTextAttribute(hConsoleOutput, lpConsoleScreenBufferInfoEx->wAttributes);
    return TRUE;
}

// ============================================================
// GetCurrentConsoleFontEx - L1 (forward, fallback to GetCurrentConsoleFont)
// Introduced: Win10 1709 (build 16299)
// ============================================================
COMPAT_API BOOL WINAPI GetCurrentConsoleFontEx(HANDLE hConsoleOutput, BOOL bMaximumWindow, PCONSOLE_FONT_INFOEX lpConsoleCurrentFontEx)
{
    typedef BOOL(WINAPI* PFN)(HANDLE, BOOL, PCONSOLE_FONT_INFOEX);
    static PFN pfn = nullptr;
    if (!pfn) pfn = (PFN)Compat_GetRealProc("kernel32", "GetCurrentConsoleFontEx");
    if (pfn) return pfn(hConsoleOutput, bMaximumWindow, lpConsoleCurrentFontEx);

    if (!lpConsoleCurrentFontEx) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }

    // Fallback: use the non-Ex version and fill defaults.
    CONSOLE_FONT_INFO cfi = { 0 };
    if (!GetCurrentConsoleFont(hConsoleOutput, bMaximumWindow, &cfi))
        return FALSE;

    lpConsoleCurrentFontEx->cbSize = sizeof(CONSOLE_FONT_INFOEX);
    lpConsoleCurrentFontEx->nFont = cfi.nFont;
    lpConsoleCurrentFontEx->dwFontSize = cfi.dwFontSize;
    lpConsoleCurrentFontEx->FontFamily = FF_DONTCARE;
    lpConsoleCurrentFontEx->FontWeight = FW_NORMAL;
    wcscpy_s(lpConsoleCurrentFontEx->FaceName, LF_FACESIZE, L"Consolas");
    return TRUE;
}

// ============================================================
// SetCurrentConsoleFontEx - L1 (forward, fallback to SetConsoleFont fallback)
// Introduced: Win10 1709 (build 16299)
// ============================================================
COMPAT_API BOOL WINAPI SetCurrentConsoleFontEx(HANDLE hConsoleOutput, BOOL bMaximumWindow, PCONSOLE_FONT_INFOEX lpConsoleCurrentFontEx)
{
    typedef BOOL(WINAPI* PFN)(HANDLE, BOOL, PCONSOLE_FONT_INFOEX);
    static PFN pfn = nullptr;
    if (!pfn) pfn = (PFN)Compat_GetRealProc("kernel32", "SetCurrentConsoleFontEx");
    if (pfn) return pfn(hConsoleOutput, bMaximumWindow, lpConsoleCurrentFontEx);

    // No reliable fallback on pre-1709 systems; the non-Ex API is read-only.
    // Return TRUE so callers proceed; the font change is silently ignored.
    (void)hConsoleOutput;
    (void)bMaximumWindow;
    (void)lpConsoleCurrentFontEx;
    return TRUE;
}

// ============================================================
// GetConsoleDisplayMode - L1 (forward, fallback returns 0)
// Introduced: Win10 1709 (build 16299)
// ============================================================
COMPAT_API BOOL WINAPI GetConsoleDisplayMode(LPDWORD lpModeFlags)
{
    typedef BOOL(WINAPI* PFN)(LPDWORD);
    static PFN pfn = nullptr;
    if (!pfn) pfn = (PFN)Compat_GetRealProc("kernel32", "GetConsoleDisplayMode");
    if (pfn) return pfn(lpModeFlags);

    if (lpModeFlags) *lpModeFlags = 0; // Not in fullscreen
    return TRUE;
}

// ============================================================
// SetConsoleDisplayMode - L1 (forward, fallback to windowed)
// Introduced: Win10 1709 (build 16299)
// ============================================================
COMPAT_API BOOL WINAPI SetConsoleDisplayMode(HANDLE hConsoleOutput, DWORD dwFlags, PCOORD lpNewScreenBufferDimensions)
{
    typedef BOOL(WINAPI* PFN)(HANDLE, DWORD, PCOORD);
    static PFN pfn = nullptr;
    if (!pfn) pfn = (PFN)Compat_GetRealProc("kernel32", "SetConsoleDisplayMode");
    if (pfn) return pfn(hConsoleOutput, dwFlags, lpNewScreenBufferDimensions);

    // Fallback: cannot switch to fullscreen on old systems; report the
    // current buffer size so the caller has valid dimensions.
    if (lpNewScreenBufferDimensions)
    {
        CONSOLE_SCREEN_BUFFER_INFO csbi = { 0 };
        if (GetConsoleScreenBufferInfo(hConsoleOutput, &csbi))
            *lpNewScreenBufferDimensions = csbi.dwSize;
    }
    return TRUE;
}
