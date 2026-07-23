// test_runtime.cpp - Validates that CompatRuntime APIs behave correctly.
// This is a simple self-verifying test; no external test framework required.

#include "../CompatRuntime/CompatRuntime.h"
#include <cstdio>
#include <cstdlib>

static int g_passed = 0;
static int g_failed = 0;

#define TEST(name) static void test_##name();     static struct Register_##name { Register_##name() { test_##name(); } } reg_##name;     static void test_##name()

#define ASSERT(expr) do {     if (!(expr)) {         printf("  FAIL: %s (line %d)\n", #expr, __LINE__);         g_failed++;     } else {         g_passed++;     } } while(0)

#define ASSERT_EQ(a, b) do {     auto _a = (a); auto _b = (b);     if (_a != _b) {         printf("  FAIL: %s == %s (got %d vs %d, line %d)\n", #a, #b, (int)_a, (int)_b, __LINE__);         g_failed++;     } else {         g_passed++;     } } while(0)

// ============================================================
// Thread API tests
// ============================================================

// Stub declarations (actual implementations are in the DLL).
extern "C" {
COMPAT_API HRESULT WINAPI SetThreadDescription(HANDLE hThread, PCWSTR lpThreadDescription);
COMPAT_API HRESULT WINAPI GetThreadDescription(HANDLE hThread, PWSTR* ppszThreadDescription);
COMPAT_API BOOL WINAPI SetThreadInformation(HANDLE hThread, THREAD_INFORMATION_CLASS ThreadInformationClass, LPVOID lpThreadInformation, DWORD dwSize);
COMPAT_API BOOL WINAPI GetThreadInformation(HANDLE hThread, THREAD_INFORMATION_CLASS ThreadInformationClass, LPVOID lpThreadInformation, DWORD dwSize);
}

TEST(SetThreadDescription_returns_S_OK)
{
    HRESULT hr = SetThreadDescription(GetCurrentThread(), L"test thread");
    ASSERT_EQ(hr, S_OK);
}

TEST(GetThreadDescription_returns_stored)
{
    // SetThreadDescription was called earlier with "test thread".
    // Our real implementation stores and retrieves it.
    // NOTE: GetThreadDescription allocates via LocalAlloc; caller must free.
    PWSTR desc = nullptr;
    HRESULT hr = GetThreadDescription(GetCurrentThread(), &desc);
    ASSERT_EQ(hr, S_OK);
    ASSERT(desc != nullptr);
    if (desc)
    {
        ASSERT(wcscmp(desc, L"test thread") == 0);
        LocalFree(desc);
    }
}

TEST(GetThreadDescription_null_ptr)
{
    HRESULT hr = GetThreadDescription(GetCurrentThread(), nullptr);
    ASSERT_EQ(hr, E_INVALIDARG);
}

// ============================================================
// Time API tests
// ============================================================

extern "C" {
COMPAT_API VOID WINAPI GetSystemTimePreciseAsFileTime(LPFILETIME lpSystemTimeAsFileTime);
COMPAT_API VOID WINAPI QueryInterruptTimePrecise(PULONGLONG lpInterruptTimePrecise);
}

TEST(GetSystemTimePreciseAsFileTime_returns_valid)
{
    FILETIME ft1 = {0}, ft2 = {0};
    GetSystemTimePreciseAsFileTime(&ft1);
    ASSERT(ft1.dwLowDateTime != 0 || ft1.dwHighDateTime != 0);
    // Second call must return >= first (time progresses monotonically).
    GetSystemTimePreciseAsFileTime(&ft2);
    ULONGLONG t1 = ((ULONGLONG)ft1.dwHighDateTime << 32) | ft1.dwLowDateTime;
    ULONGLONG t2 = ((ULONGLONG)ft2.dwHighDateTime << 32) | ft2.dwLowDateTime;
    ASSERT(t2 >= t1);
}

TEST(QueryInterruptTimePrecise_returns_nonzero)
{
    ULONGLONG time = 0;
    QueryInterruptTimePrecise(&time);
    ASSERT(time > 0);
}

// ============================================================
// DPI API tests
// ============================================================

extern "C" {
COMPAT_API UINT WINAPI GetDpiForWindow(HWND hwnd);
COMPAT_API UINT WINAPI GetDpiForSystem();
COMPAT_API BOOL WINAPI SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT value);
COMPAT_API BOOL WINAPI EnableNonClientDpiScaling(HWND hwnd);
}

TEST(GetDpiForSystem_returns_96_or_more)
{
    UINT dpi = GetDpiForSystem();
    ASSERT(dpi >= 96);
}

TEST(SetProcessDpiAwarenessContext_returns_true)
{
    BOOL ok = SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE);
    ASSERT(ok);
}

TEST(EnableNonClientDpiScaling_returns_true)
{
    BOOL ok = EnableNonClientDpiScaling(NULL);
    ASSERT(ok);
}

// ============================================================
// DLL Directory API tests
// ============================================================

extern "C" {
COMPAT_API PVOID WINAPI AddDllDirectory(PCWSTR NewDirectory);
COMPAT_API BOOL WINAPI RemoveDllDirectory(PVOID Cookie);
COMPAT_API BOOL WINAPI SetDefaultDllDirectories(DWORD DirectoryFlags);
}

TEST(AddDllDirectory_returns_nonnull)
{
    PVOID cookie = AddDllDirectory(L"C:\\NonexistentTestPath");
    ASSERT(cookie != nullptr);
    if (cookie) RemoveDllDirectory(cookie);
}

TEST(SetDefaultDllDirectories_returns_true)
{
    BOOL ok = SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    ASSERT(ok);
}

// ============================================================
// Condition Variable API tests
// ============================================================

extern "C" {
COMPAT_API VOID WINAPI InitializeConditionVariable(PCONDITION_VARIABLE ConditionVariable);
COMPAT_API VOID WINAPI WakeConditionVariable(PCONDITION_VARIABLE ConditionVariable);
COMPAT_API VOID WINAPI WakeAllConditionVariable(PCONDITION_VARIABLE ConditionVariable);
COMPAT_API BOOL WINAPI SleepConditionVariableCS(PCONDITION_VARIABLE cv, PCRITICAL_SECTION cs, DWORD ms);
}

TEST(InitializeConditionVariable_does_not_crash)
{
    CONDITION_VARIABLE cv = {0};
    InitializeConditionVariable(&cv);
    ASSERT(cv.Ptr != nullptr);
    // Cleanup: set event so any waiting thread can exit.
    if (cv.Ptr) WakeConditionVariable(&cv);
}

// ============================================================
// Main
// ============================================================

int main()
{
    printf("========================================\n");
    printf(" CompatRuntime Test Suite\n");
    printf("========================================\n\n");

    // Tests run via static initialization above.
    // Print results.
    printf("\n========================================\n");
    printf(" Results: %d passed, %d failed\n", g_passed, g_failed);
    printf("========================================\n");

    return g_failed > 0 ? 1 : 0;
}
