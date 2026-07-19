// DLLProvider.cpp - DLL directory API compatibility implementations.
// AddDllDirectory -> L0 (maintain a list of directories)
// RemoveDllDirectory -> L0 (remove from list)
// SetDefaultDllDirectories -> L0 (set flags)

#include "../CompatRuntime.h"
#include <stdlib.h>

// Simple linked list of added DLL directories.
typedef struct _DLL_DIR_NODE {
    WCHAR path[MAX_PATH];
    struct _DLL_DIR_NODE* next;
} DLL_DIR_NODE;

static DLL_DIR_NODE* g_DllDirHead = NULL;
static DWORD g_DllDirFlags = LOAD_LIBRARY_SEARCH_DEFAULT_DIRS;

// ============================================================
// AddDllDirectory - L0 (maintain internal list)
// Introduced: Win10 1607 (build 14393)
// ============================================================
COMPAT_API PVOID WINAPI AddDllDirectory(PCWSTR NewDirectory)
{
    if (!NewDirectory) { SetLastError(ERROR_INVALID_PARAMETER); return NULL; }
    DLL_DIR_NODE* node = (DLL_DIR_NODE*)HeapAlloc(GetProcessHeap(), 0, sizeof(DLL_DIR_NODE));
    if (!node) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
    wcscpy_s(node->path, MAX_PATH, NewDirectory);
    node->next = g_DllDirHead;
    g_DllDirHead = node;
    return (PVOID)node;
}

// ============================================================
// RemoveDllDirectory - L0
// Introduced: Win10 1607 (build 14393)
// ============================================================
COMPAT_API BOOL WINAPI RemoveDllDirectory(PVOID Cookie)
{
    if (!Cookie) return FALSE;
    DLL_DIR_NODE* prev = NULL;
    DLL_DIR_NODE* cur = g_DllDirHead;
    while (cur)
    {
        if (cur == (DLL_DIR_NODE*)Cookie)
        {
            if (prev) prev->next = cur->next;
            else g_DllDirHead = cur->next;
            HeapFree(GetProcessHeap(), 0, cur);
            return TRUE;
        }
        prev = cur;
        cur = cur->next;
    }
    return FALSE;
}

// ============================================================
// SetDefaultDllDirectories - L0 (store flags)
// Introduced: Win10 1607 (build 14393)
// ============================================================
COMPAT_API BOOL WINAPI SetDefaultDllDirectories(DWORD DirectoryFlags)
{
    g_DllDirFlags = DirectoryFlags;
    return TRUE;
}
