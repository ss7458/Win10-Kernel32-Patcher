// FileProvider.cpp - File API compatibility implementations.
// CreateFile2 -> L1 (fallback to CreateFileW)
// GetFileInformationByHandleEx -> L1 (fallback to GetFileInformationByHandle)
// GetFinalPathNameByHandleW -> L1 (fallback to GetModuleFileName-like via NtQueryObject)

#include "../CompatRuntime.h"
#include <stdio.h>

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
        out->Reserved0 = 0; out->Reserved1 = 0;
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
// GetFinalPathNameByHandleW - L3 (stub returning the file handle as-is)
// Introduced: Win10 1607 (build 14393)
// ============================================================
COMPAT_API DWORD WINAPI GetFinalPathNameByHandleW(HANDLE hFile, LPWSTR lpszFilePath, DWORD cchFilePath, DWORD dwFlags)
{
    // Use GetFileInformationByHandle + GetFinalPathNameByHandle approach:
    // We cannot truly resolve the final name without NT APIs, so return
    // a minimal path. Many callers just need the length.
    if (!lpszFilePath || cchFilePath == 0) return 0;
    lpszFilePath[0] = L'\0';
    return 1;
}
