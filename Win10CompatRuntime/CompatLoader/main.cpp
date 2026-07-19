// CompatLoader.cpp - Runtime loader / injector for the Windows 10
// Compatibility Runtime.
//
// Usage:
//   CompatLoader.exe <target.exe> [--database <dir>] [--compat <CompatRuntime.dll>]
//
// Launches <target.exe> in a suspended state, injects CompatRuntime.dll into it
// via a LoadLibraryA remote thread, then resumes the target. CompatRuntime.dll's
// DllMain detects the COMPAT_LOADER environment flag and rewrites the target's
// IAT so its kernel32 / api-ms imports resolve to the compatibility shims.

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>

static std::string AbsolutePath(const char* p)
{
    char buf[MAX_PATH] = { 0 };
    if (!GetFullPathNameA(p, MAX_PATH, buf, nullptr))
        return std::string(p);
    return std::string(buf);
}

int main(int argc, char* argv[])
{
    const char* target = nullptr;
    std::string dbDir = "Database";
    std::string compat = "CompatRuntime.dll";

    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--database") && i + 1 < argc)
            dbDir = argv[++i];
        else if (!strcmp(argv[i], "--compat") && i + 1 < argc)
            compat = argv[++i];
        else if (!target)
            target = argv[i];
    }

    if (!target)
    {
        printf("Usage: CompatLoader <target.exe> [--database <dir>] [--compat <CompatRuntime.dll>]\n");
        return 1;
    }

    std::string absCompat = AbsolutePath(compat.c_str());
    std::string absDb = AbsolutePath(dbDir.c_str());

    // Pass control flags to the child via the environment (inherited by
    // CreateProcess when lpEnvironment is NULL).
    std::string e1 = "COMPAT_LOADER=1";
    std::string e2 = std::string("COMPAT_DB=") + absDb;
    _putenv(e1.c_str());
    _putenv(e2.c_str());

    // Forward an optional diagnostic flag so the injected DLL can report how
    // many IAT imports it redirected.
    char diagBuf[64] = { 0 };
    if (getenv("COMPAT_DIAG"))
    {
        _putenv("COMPAT_DIAG=1");
        (void)diagBuf;
    }

    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };
    if (!CreateProcessA(target, nullptr, nullptr, nullptr, TRUE,
                        CREATE_SUSPENDED, nullptr, nullptr, &si, &pi))
    {
        printf("[ERROR] CreateProcess failed: %lu\n", GetLastError());
        return 1;
    }

    // Inject CompatRuntime.dll into the suspended target via a LoadLibraryA
    // remote thread.
    SIZE_T pathLen = absCompat.size() + 1;
    LPVOID remote = VirtualAllocEx(pi.hProcess, nullptr, pathLen,
                                   MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote)
    {
        printf("[ERROR] VirtualAllocEx: %lu\n", GetLastError());
        goto cleanup;
    }
    if (!WriteProcessMemory(pi.hProcess, remote, absCompat.c_str(), pathLen, nullptr))
    {
        printf("[ERROR] WriteProcessMemory: %lu\n", GetLastError());
        goto cleanup;
    }

    // LoadLibraryA lives in kernel32.dll, a "known DLL" mapped at the same base
    // address in every process of this session, so its address here is valid
    // inside the target too.
    HMODULE hKernel = GetModuleHandleA("kernel32.dll");
    FARPROC pLoadLibraryA = GetProcAddress(hKernel, "LoadLibraryA");
    if (!pLoadLibraryA)
    {
        printf("[ERROR] GetProcAddress(LoadLibraryA) failed\n");
        goto cleanup;
    }

    HANDLE hThread = CreateRemoteThread(pi.hProcess, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(pLoadLibraryA),
        remote, 0, nullptr);
    if (!hThread)
    {
        printf("[ERROR] CreateRemoteThread: %lu\n", GetLastError());
        goto cleanup;
    }

    WaitForSingleObject(hThread, INFINITE);
    DWORD loadResult = 0;
    GetExitCodeThread(hThread, &loadResult);
    CloseHandle(hThread);

    if (!loadResult)
    {
        printf("[ERROR] CompatRuntime.dll failed to load in target "
               "(LoadLibraryA returned 0).\n");
        goto cleanup;
    }

    printf("[INFO] CompatRuntime.dll injected; IAT redirection applied by "
           "DllMain trigger.\n");

cleanup:
    if (remote)
        VirtualFreeEx(pi.hProcess, remote, 0, MEM_RELEASE);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}
