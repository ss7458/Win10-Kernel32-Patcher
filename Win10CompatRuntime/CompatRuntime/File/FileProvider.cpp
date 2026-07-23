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
    case 7: // FileStreamInfo — no alternate data stream support; return minimal info
    {
        // FILE_STREAM_INFO requires at least enough space for one entry.
        if (dwBufferSize < sizeof(FILE_STREAM_INFO)) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
        FILE_STREAM_INFO* out = (FILE_STREAM_INFO*)lpFileInformation;
        // Zero the struct (no streams beyond the default).
        memset(out, 0, sizeof(FILE_STREAM_INFO));
        out->StreamNameLength = 0;
        out->StreamSize.QuadPart = 0;
        out->StreamAllocationSize.QuadPart = 0;
        out->NextEntryOffset = 0;
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
    // QueryDosDeviceW(DOS_name -> NT_path), so we enumerate drive letters
    // and find which one maps to our NT device path.
    const std::wstring devPrefix = L"\\Device\\HarddiskVolume";
    if (nt.compare(0, devPrefix.size(), devPrefix) != 0)
        return nt; // not a disk file; return raw NT path as best effort

    size_t hv = nt.find(L"HarddiskVolume");
    if (hv == std::wstring::npos) return nt;
    size_t end = nt.find(L'\\', hv);
    std::wstring volNode = nt.substr(0, (end == std::wstring::npos) ? std::wstring::npos : end);

    // Enumerate drive letters A:-Z: and find the one whose DOS device name
    // resolves to the same NT device path.
    WCHAR driveLetter[4] = { 0 };
    bool found = false;
    WCHAR dosDeviceBuf[256] = { 0 };
    for (wchar_t ch = L'A'; ch <= L'Z'; ++ch)
    {
        WCHAR driveName[4] = { ch, L':', L'\0' };
        if (QueryDosDeviceW(driveName, dosDeviceBuf, ARRAYSIZE(dosDeviceBuf)) != 0)
        {
            if (volNode == dosDeviceBuf)
            {
                driveLetter[0] = ch;
                driveLetter[1] = L':';
                driveLetter[2] = L'\0';
                found = true;
                break;
            }
        }
    }
    if (!found)
        return nt; // mapping failed; best effort

    std::wstring rest = (end == std::wstring::npos) ? L"" : nt.substr(end);
    return std::wstring(L"\\\\?\\") + driveLetter + rest;
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
    case 0: // FileBasicInfo — benign no-op (read-only class)
    case 5: // FileAllocationInfo — benign no-op
        return TRUE;
    case 3: // FileRenameInfo — write operation, cannot emulate
    case 4: // FileDispositionInfo — write operation, cannot emulate
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

// ============================================================
// OpenFileById - L1 (forward, fallback via NtCreateFile with FileId)
// Introduced: Win10 1607 (build 14393)
// Opens a file by its 64-bit FileID on a given volume handle.
// ============================================================

// FILE_OPEN_BY_FILE_ID is defined in ntifs.h (kernel-mode). Define for user-mode.
#ifndef FILE_OPEN_BY_FILE_ID
#define FILE_OPEN_BY_FILE_ID 0x00002000
#endif

COMPAT_API HANDLE WINAPI OpenFileById(HANDLE hVolumeHint, LPFILE_ID_DESCRIPTOR lpFileId, DWORD dwDesiredAccess,
    DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwFlagsAndAttributes)
{
    typedef HANDLE(WINAPI* PFN)(HANDLE, LPFILE_ID_DESCRIPTOR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD);
    static PFN pfn = nullptr;
    if (!pfn) pfn = (PFN)Compat_GetRealProc("kernel32", "OpenFileById");
    if (pfn) return pfn(hVolumeHint, lpFileId, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwFlagsAndAttributes);

    if (!lpFileId) { SetLastError(ERROR_INVALID_PARAMETER); return INVALID_HANDLE_VALUE; }

    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (!ntdll) ntdll = LoadLibraryA("ntdll.dll");
    if (!ntdll) { SetLastError(ERROR_NOT_SUPPORTED); return INVALID_HANDLE_VALUE; }

    typedef LONG(NTAPI* NtCreateFile_t)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK,
        PLARGE_INTEGER, ULONG, ULONG, ULONG, ULONG, PVOID, ULONG);
    auto NtCreateFile = (NtCreateFile_t)GetProcAddress(ntdll, "NtCreateFile");
    if (!NtCreateFile) { SetLastError(ERROR_NOT_SUPPORTED); return INVALID_HANDLE_VALUE; }

    UNICODE_STRING uniName = { 0 };
    OBJECT_ATTRIBUTES objAttr = { 0 };

    // Extract the 64-bit FileID. Only FileIdType is supported.
    ULONGLONG fileId = 0;
    if (lpFileId->Type == FileIdType)
        fileId = lpFileId->FileId.QuadPart;
    else
    {
        // ObjectIdType and other types not supported in fallback path.
        SetLastError(ERROR_NOT_SUPPORTED);
        return INVALID_HANDLE_VALUE;
    }

    uniName.Length = sizeof(ULONGLONG);
    uniName.MaximumLength = sizeof(ULONGLONG);
    uniName.Buffer = (PWSTR)&fileId;

    objAttr.Length = sizeof(OBJECT_ATTRIBUTES);
    objAttr.RootDirectory = hVolumeHint;
    objAttr.ObjectName = &uniName;
    objAttr.Attributes = OBJ_CASE_INSENSITIVE;

    IO_STATUS_BLOCK ioStatus = { 0 };
    HANDLE hFile = nullptr;
    ULONG createOptions = FILE_OPEN_BY_FILE_ID;

    LONG status = NtCreateFile(&hFile, dwDesiredAccess, &objAttr, &ioStatus,
        nullptr, dwFlagsAndAttributes, dwShareMode, FILE_OPEN, createOptions,
        nullptr, 0);

    if (status < 0)
    {
        typedef ULONG(NTAPI* RtlNtStatusToDosError_t)(LONG);
        auto pRtlNtStatusToDosError = (RtlNtStatusToDosError_t)GetProcAddress(ntdll, "RtlNtStatusToDosError");
        if (pRtlNtStatusToDosError)
            SetLastError(pRtlNtStatusToDosError(status));
        else
            SetLastError(ERROR_NOT_SUPPORTED);
        return INVALID_HANDLE_VALUE;
    }
    return hFile;
}

// ============================================================
// SetFileIoOverlappedRange - L1 (forward, fallback benign)
// Introduced: Win10 1607 (build 14393)
// ============================================================
COMPAT_API BOOL WINAPI SetFileIoOverlappedRange(HANDLE hFile, PUCHAR OverlappedRange, ULONG Length)
{
    typedef BOOL(WINAPI* PFN)(HANDLE, PUCHAR, ULONG);
    static PFN pfn = nullptr;
    if (!pfn) pfn = (PFN)Compat_GetRealProc("kernel32", "SetFileIoOverlappedRange");
    if (pfn) return pfn(hFile, OverlappedRange, Length);

    // Fallback: not supported on older systems; return TRUE so callers proceed.
    return TRUE;
}

// ============================================================
// GetFileInformationByHandleEx - already implemented above.
// ============================================================

// ============================================================
// CopyFileExW - L1 (forward, fallback to CopyFileW with progress callback simulation)
// Introduced: Win10 1607 (build 14393) [kernelbase variant]
// ============================================================

// LPPROGRESS_ROUTINE callback reason values (not defined in older SDK headers).
#ifndef COPYFILE_CALLBACK_CHUNK_STARTED
#define COPYFILE_CALLBACK_CHUNK_STARTED 0
#endif
#ifndef COPYFILE_CALLBACK_CHUNK_FINISHED
#define COPYFILE_CALLBACK_CHUNK_FINISHED 1
#endif

COMPAT_API BOOL WINAPI CopyFileExW(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName,
    LPPROGRESS_ROUTINE lpProgressRoutine, LPVOID lpData, LPBOOL pbCancel, DWORD dwCopyFlags)
{
    typedef BOOL(WINAPI* PFN)(LPCWSTR, LPCWSTR, LPPROGRESS_ROUTINE, LPVOID, LPBOOL, DWORD);
    static PFN pfn = nullptr;
    if (!pfn) pfn = (PFN)Compat_GetRealProc("kernel32", "CopyFileExW");
    if (pfn) return pfn(lpExistingFileName, lpNewFileName, lpProgressRoutine, lpData, pbCancel, dwCopyFlags);

    // Fallback: use CopyFileW. If a progress callback is provided, we
    // simulate a single progress notification (100% complete).
    BOOL result = CopyFileW(lpExistingFileName, lpNewFileName, dwCopyFlags & COPY_FILE_FAIL_IF_EXISTS);
    if (result && lpProgressRoutine)
    {
        // Get file size for the callback.
        HANDLE hFile = CreateFileW(lpExistingFileName, GENERIC_READ, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, 0, nullptr);
        if (hFile != INVALID_HANDLE_VALUE)
        {
            LARGE_INTEGER size = { 0 };
            GetFileSizeEx(hFile, &size);
            lpProgressRoutine(size, size, size, size, 1, COPYFILE_CALLBACK_CHUNK_FINISHED,
                hFile, hFile, lpData);
            CloseHandle(hFile);
        }
    }
    return result;
}

// ============================================================
// MoveFileExW - L1 (forward, fallback to MoveFileW with flags)
// Introduced: Win10 1607 (build 14393) [kernelbase variant]
// ============================================================
COMPAT_API BOOL WINAPI MoveFileExW(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName, DWORD dwFlags)
{
    typedef BOOL(WINAPI* PFN)(LPCWSTR, LPCWSTR, DWORD);
    static PFN pfn = nullptr;
    if (!pfn) pfn = (PFN)Compat_GetRealProc("kernel32", "MoveFileExW");
    if (pfn) return pfn(lpExistingFileName, lpNewFileName, dwFlags);

    // Fallback: MoveFileW doesn't support flags. Handle MOVEFILE_REPLACE_EXISTING
    // by deleting the destination first if needed. Note: this is not atomic on
    // very old systems; on XP+ MoveFileExW always exists in kernel32 so the
    // fallback path is unlikely to be reached.
    if (dwFlags & MOVEFILE_REPLACE_EXISTING)
    {
        if (GetFileAttributesW(lpNewFileName) != INVALID_FILE_ATTRIBUTES)
            DeleteFileW(lpNewFileName);
    }
    return MoveFileW(lpExistingFileName, lpNewFileName);
}
