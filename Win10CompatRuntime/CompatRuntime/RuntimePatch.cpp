// RuntimePatch.cpp
//
// Runtime IAT redirection for the host EXE.
//
// When CompatRuntime.dll is injected into a target process (by CompatLoader.exe)
// and the COMPAT_LOADER environment flag is set, DllMain triggers this code to
// rewrite the host EXE's own import address table: any import whose API name
// appears in the compatibility database is redirected to the matching export of
// CompatRuntime.dll.
//
// This handles both kernel32.dll and api-ms-win-* imports uniformly, because the
// OS loader has already resolved api-ms imports to their real implementation
// (kernel32/kernelbase) and filled the IAT with real addresses; the import name
// table still carries the original API name, which is what we match on.

#include "CompatRuntime.h"

#include <windows.h>
#include <winnt.h>

#include <cstring>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace {

// Load the set of API names that CompatRuntime provides shims for, by scanning
// the same database CSV files used by the static patcher (CompatPatch).
std::vector<std::string> LoadDatabaseNames(const char* dbDir)
{
    std::vector<std::string> names;
    static const char* const kFiles[] = {
        "kernel32.csv", "kernelbase.csv", "user32.csv",
        "advapi32.csv", "api-ms.csv", nullptr
    };

    for (int i = 0; kFiles[i]; ++i)
    {
        std::string path = std::string(dbDir ? dbDir : ".") + "\\" + kFiles[i];
        std::ifstream f(path.c_str());
        if (!f.is_open())
            continue;

        std::string line;
        while (std::getline(f, line))
        {
            if (line.empty() || line[0] == '#')
                continue;

            // Format: module,name,introduced,level,strategy,runtime
            size_t c1 = line.find(',');
            if (c1 == std::string::npos)
                continue;
            size_t c2 = line.find(',', c1 + 1);
            if (c2 == std::string::npos)
                continue;

            std::string name = line.substr(c1 + 1, c2 - (c1 + 1));
            if (name.size() >= 2 && name.front() == '"' && name.back() == '"')
                name = name.substr(1, name.size() - 2);
            if (!name.empty())
                names.push_back(name);
        }
    }
    return names;
}

// Rewrite the host EXE's import table in memory. Returns true if the table was
// located and walked (not necessarily that every entry was patched).
bool PatchCurrentProcess(const char* dbDir)
{
    HMODULE hHost = GetModuleHandleA(nullptr); // the EXE module
    if (!hHost)
        return false;

    BYTE* base = reinterpret_cast<BYTE*>(hHost);
    IMAGE_DOS_HEADER* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE)
        return false;

    IMAGE_NT_HEADERS* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (!nt || nt->Signature != IMAGE_NT_SIGNATURE)
        return false;

    IMAGE_OPTIONAL_HEADER64* opt =
        reinterpret_cast<IMAGE_OPTIONAL_HEADER64*>(&nt->OptionalHeader);
    DWORD impRva = opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!impRva)
        return false;

    HMODULE hCompat = GetModuleHandleA("CompatRuntime.dll");
    if (!hCompat)
        return false;

    std::vector<std::string> db = LoadDatabaseNames(dbDir);
    if (db.empty())
        return false;

    bool diag = (getenv("COMPAT_DIAG") != nullptr);
    int patched = 0;

    IMAGE_IMPORT_DESCRIPTOR* desc =
        reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + impRva);

    for (; desc->Name; ++desc)
    {
        IMAGE_THUNK_DATA64* oft = desc->OriginalFirstThunk
            ? reinterpret_cast<IMAGE_THUNK_DATA64*>(base + desc->OriginalFirstThunk)
            : reinterpret_cast<IMAGE_THUNK_DATA64*>(base + desc->FirstThunk);
        IMAGE_THUNK_DATA64* ft =
            reinterpret_cast<IMAGE_THUNK_DATA64*>(base + desc->FirstThunk);
        if (!ft || !oft)
            continue;

        for (SIZE_T i = 0; ft[i].u1.AddressOfData; ++i)
        {
            if (ft[i].u1.Ordinal & IMAGE_ORDINAL_FLAG64)
                continue; // ordinal import: cannot match by name

            IMAGE_IMPORT_BY_NAME* ibn =
                reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + oft[i].u1.AddressOfData);
            const char* apiName = reinterpret_cast<const char*>(ibn->Name);
            if (!apiName)
                continue;

            bool inDb = false;
            for (const std::string& n : db)
            {
                if (_stricmp(n.c_str(), apiName) == 0)
                {
                    inDb = true;
                    break;
                }
            }
            if (!inDb)
                continue;

            FARPROC fn = GetProcAddress(hCompat, apiName);
            if (!fn)
                continue; // no shim for this API: leave original

            DWORD oldProtect = 0;
            if (VirtualProtect(&ft[i].u1.Function, sizeof(ULONG64),
                               PAGE_READWRITE, &oldProtect))
            {
                ft[i].u1.Function = reinterpret_cast<ULONG64>(fn);
                VirtualProtect(&ft[i].u1.Function, sizeof(ULONG64),
                               oldProtect, &oldProtect);
                ++patched;
                if (diag)
                    printf("[DIAG] redirected host import: %s\n", apiName);
            }
        }
    }

    if (diag)
        printf("[DIAG] CompatRuntime redirected %d IAT import(s) in host EXE.\n", patched);

    return true;
}

} // namespace

// Internal entry point used by DllMain when the COMPAT_LOADER trigger is set.
void Compat_RuntimePatchCurrentProcess(const char* dbDir)
{
    PatchCurrentProcess(dbDir);
}

// Exported entry point for explicit / test use: apply redirection to the
// current process on demand (caller is responsible for ensuring the host EXE
// imports the relevant APIs).
COMPAT_API void WINAPI Compat_ApplyToCurrentProcess(const char* dbDir)
{
    PatchCurrentProcess(dbDir);
}
