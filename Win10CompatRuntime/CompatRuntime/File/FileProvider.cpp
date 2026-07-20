// FileProvider.cpp - File API compatibility implementations.
// CreateFile2 -> L1 (fallback to CreateFileW)
// GetFileInformationByHandleEx -> L1 (fallback to GetFileInformationByHandle)
// GetFinalPathNameByHandleW -> L1 (fallback to GetModuleFileName-like via NtQueryObject)

#include "../CompatRuntime.h"
#include <stdio.h>
#include <psapi.h>
#include <vector>
#include <string>
#pragma comment(lib, "psapi.lib")

// ============================================================
// CreateFile2 - L1 (fallback to CreateFileW)
// Introduced: Win10 1607 (build 14393)
// ============================================================
COMPAT_API HANDLE WINAPI CreateFile2(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
    DWORD dwCreationDisposition, CREATEFILE2_EXTENDED_PARAMETERS* pCreateExParams)
{
    DWORD flagsAndAttributes = 0;
    HANDLE hTemplateFile = NULL;
    SECURITY_ATTRIBUTES sa = { 0 };
    sa.nLength = sizeof(sa);
    if (pCreateExParams)
    {
        flagsAndAttributes = pCreateExParams->dwFileFlags | pCreateExParams->dwFileAttributes;
        hTemplateFile = pCreateExParams->hTemplateFile;
        if (pCreateExParams->lpSecurityAttributes) sa = *(pCreateExParams->lpSecurityAttributes);
    }
    return CreateFileW(lpFileName, dwDesiredAccess, dwShareMode,
        (pCreateExParams && pCreateExParams->lpSecurityAttributes) ? &sa : NULL,
        dwCreationDisposition, flagsAndAttributes, hTemplateFile);
}

// ============================================================
// GetFileInformationByHandleEx - L1 (partial fallback)
// Introduced: Win10 1607 (build 14393)
// ============================================================
COMPAT_API BOOL WINAPI GetFileInformationByHandleEx(HANDLE hFile, FILE_INFO_BY_HANDLE_CLASS FileInformationClass,
    LPVOID lpFileInformation, DWORD dwBufferSize)
{
    switch (FileInformationClass)
    {
    case 0: // FileBasicInfo
    {
        BY_HANDLE_FILE_INFORMATION info = { 0 };
        if (!GetFileInformationByHandle(hFile, &info)) return FALSE;
        FILE_BASIC_INFO* out = (FILE_BASIC_INFO*)lpFileInformation;
        if (dwBufferSize < sizeof(FILE_BASIC_INFO)) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
        out->CreationTime.LowPart = info.ftCreationTime.dwLowDateTime;
        out->CreationTime.HighPart = info.ftCreationTime.dwHighDateTime;
        out->LastAccessTime.LowPart = info.ftLastAccessTime.dwLowDateTime;
        out->LastAccessTime.HighPart = info.ftLastAccessTime.dwHighDateTime;
        out->LastWriteTime.LowPart = info.ftLastWriteTime.dwLowDateTime;
        out->LastWriteTime.HighPart = info.ftLastWriteTime.dwHighDateTime;
        out->ChangeTime.LowPart = info.ftLastWriteTime.dwLowDateTime;
        out->ChangeTime.HighPart = info.ftLastWriteTime.dwHighDateTime;
        out->FileAttributes = info.dwFileAttributes;
        return TRUE;
    }
    case 4: // FileStandardInfo
    {
        BY_HANDLE_FILE_INFORMATION info = { 0 };
        if (!GetFileInformationByHandle(hFile, &info)) return FALSE;
        FILE_STANDARD_INFO* out = (FILE_STANDARD_INFO*)lpFileInformation;
        if (dwBufferSize < sizeof(FILE_STANDARD_INFO)) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
        out->AllocationSize.LowPart = info.nFileSizeLow;
        out->AllocationSize.HighPart = info.nFileSizeHigh;
        out->EndOfFile = out->AllocationSize;
        out->NumberOfLinks = info.nNumberOfLinks;
        out->DeletePending = FALSE;
        out->Directory = FALSE;
        return TRUE;
    }
    case 7: // FileStandardInfo (FileSizeInfo alias)
    {
        BY_HANDLE_FILE_INFORMATION info = { 0 };
        if (!GetFileInformationByHandle(hFile, &info)) return FALSE;
        FILE_STANDARD_INFO* out = (FILE_STANDARD_INFO*)lpFileInformation;
        if (dwBufferSize < sizeof(FILE_STANDARD_INFO)) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
        out->AllocationSize.LowPart = info.nFileSizeLow;
        out->AllocationSize.HighPart = info.nFileSizeHigh;
        out->EndOfFile = out->AllocationSize;
        return TRUE;
    }
    default:
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
}

// ============================================================
// GetFinalPathNameByHandleW - L1 (real: forward to OS API, else
//   resolve via NtQueryObject + volume device -> drive mapping)
// Introduced: Win10 1607 (build 14393)
// ============================================================
// Resolve the NT object name of a file handle via ntdll!NtQueryObject,
// then map the volume device (\Device\HarddiskVolumeN) to a DOS drive letter.
static std::wstring ResolveFinalPathFallback(HANDLE hFile)
{
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (!ntdll) ntdll = LoadLibraryA("ntdll.dll");
    if (!ntdll) return {};

    typedef LONG(NTAPI* NtQueryObject_t)(HANDLE, int, void*, ULONG, ULONG*);
    auto NtQueryObject = (NtQueryObject_t)GetProcAddress(ntdll, "NtQueryObject");
    if (!NtQueryObject) return {};

    // OBJECT_NAME_INFORMATION = { UNICODE_STRING Name; WCHAR NameBuffer[1]; }
    struct OBJECT_NAME_INFO {
        struct { USHORT Length; USHORT MaximumLength; WCHAR* Buffer; } Name;
    };

    std::wstring nt;
    ULONG size = 256;
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        std::vector<BYTE> buf(size);
        ULONG ret = 0;
        // ObjectNameInformation = 1
        LONG st = NtQueryObject(hFile, 1, buf.data(), size, &ret);
        if (st == 0) // STATUS_SUCCESS
        {
            auto* oni = reinterpret_cast<OBJECT_NAME_INFO*>(buf.data());
            if (oni->Name.Buffer && oni->Name.Length > 0)
                nt = std::wstring(oni->Name.Buffer, oni->Name.Length / sizeof(WCHAR));
            break;
        }
        if (st == 0xC0000023 || st == 0xC0000004) // INFO_LENGTH_MISMATCH / BUFFER_TOO_SMALL
        {
            size = ret ? ret + 16 : size * 2;
            continue;
        }
        break;
    }
    if (nt.empty()) return {};

    // Map \Device\HarddiskVolumeN -> drive letter.
    const std::wstring devPrefix = L"\\Device\\HarddiskVolume";
    if (nt.compare(0, devPrefix.size(), devPrefix) != 0)
        return nt; // not a disk file; return raw NT path as best effort

    size_t hv = nt.find(L"HarddiskVolume");
    if (hv == std::wstring::npos) return nt;
    size_t end = nt.find(L'\\', hv);
    std::wstring volNode = nt.substr(hv, (end == std::wstring::npos) ? std::wstring::npos : end - hv);

    WCHAR drive[4] = { 0 };
    if (QueryDosDeviceW(volNode.c_str(), drive, 4) == 0)
        return nt; // mapping failed; best effort

    std::wstring rest = (end == std::wstring::npos) ? L"" : nt.substr(end);
    return std::wstring(L"\\\\?\\") + drive + rest;
}

COMPAT_API DWORD WINAPI GetFinalPathNameByHandleW(HANDLE hFile, LPWSTR lpszFilePath, DWORD cchFilePath, DWORD dwFlags)
{
    // Prefer the real API when present (Vista+).
    typedef DWORD(WINAPI* GetFinalPathNameByHandleW_t)(HANDLE, LPWSTR, DWORD, DWORD);
    static GetFinalPathNameByHandleW_t pReal = nullptr;
    if (!pReal) pReal = (GetFinalPathNameByHandleW_t)Compat_GetRealProc("kernel32", "GetFinalPathNameByHandleW");
    if (pReal)
        return pReal(hFile, lpszFilePath, cchFilePath, dwFlags);

    // Fallback: resolve via NT object name + volume mapping.
    std::wstring path = ResolveFinalPathFallback(hFile);
    if (path.empty())
    {
        SetLastError(ERROR_NOT_SUPPORTED);
        return 0;
    }
    if (!lpszFilePath || cchFilePath == 0)
        return (DWORD)(path.size() + 1); // required size incl. null
    if (path.size() + 1 > cchFilePath)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return (DWORD)(path.size() + 1);
    }
    wcscpy_s(lpszFilePath, cchFilePath, path.c_str());
    return (DWORD)path.size(); // length excl. null
}

// ============================================================
// SetFileInformationByHandle - L2 (forward or benign stub for known classes)
// Introduced: Win7 / Win10 1607 (build 14393)
// ============================================================
COMPAT_API BOOL WINAPI SetFileInformationByHandle(HANDLE hFile, FILE_INFO_BY_HANDLE_CLASS FileInformationClass,
    LPVOID lpFileInformation, DWORD dwBufferSize)
{
    typedef BOOL(WINAPI* SetFileInformationByHandle_t)(HANDLE, FILE_INFO_BY_HANDLE_CLASS, LPVOID, DWORD);
    auto real = (SetFileInformationByHandle_t)Compat_GetRealProc("kernel32", "SetFileInformationByHandle");
    if (real) return real(hFile, FileInformationClass, lpFileInformation, dwBufferSize);

    (void)lpFileInformation;
    (void)dwBufferSize;

    switch (FileInformationClass)
    {
    case 0: // FileBasicInfo
    case 3: // FileRenameInfo
    case 4: // FileDispositionInfo
    case 5: // FileAllocationInfo
        return TRUE;
    default:
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
}

// ============================================================
// FindFirstStreamW - L1 (forward or stub - no alternate streams)
// Introduced: Vista
// ============================================================
COMPAT_API HANDLE WINAPI FindFirstStreamW(LPCWSTR lpFileName, STREAM_INFO_LEVELS InfoLevel,
    LPVOID lpFindStreamData, DWORD dwFlags)
{
    typedef HANDLE(WINAPI* FindFirstStreamW_t)(LPCWSTR, STREAM_INFO_LEVELS, LPVOID, DWORD);
    auto real = (FindFirstStreamW_t)Compat_GetRealProc("kernel32", "FindFirstStreamW");
    if (real) return real(lpFileName, InfoLevel, lpFindStreamData, dwFlags);

    (void)lpFileName;
    (void)InfoLevel;
    (void)lpFindStreamData;
    (void)dwFlags;

    SetLastError(ERROR_HANDLE_EOF);
    return INVALID_HANDLE_VALUE;
}

// ============================================================
// FindNextStreamW - L4 (not implementable - no alternate streams)
// Introduced: Vista
// ============================================================
COMPAT_API BOOL WINAPI FindNextStreamW(HANDLE hFindStream, LPVOID lpFindStreamData)
{
    (void)hFindStream;
    (void)lpFindStreamData;
    SetLastError(ERROR_HANDLE_EOF);
    return FALSE;
}
