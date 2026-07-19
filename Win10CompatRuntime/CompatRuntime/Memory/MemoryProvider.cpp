// MemoryProvider.cpp - Memory API compatibility implementations.
// VirtualAlloc2 -> L2 (dispatch: NUMA->VirtualAllocExNuma, plain->VirtualAlloc, placeholder->ERROR)
// MapViewOfFile3 -> L2 (fallback to MapViewOfFile)
// VirtualProtectFromApp -> L1 (fallback to VirtualProtect)

#include "../CompatRuntime.h"

// ============================================================
// VirtualAlloc2 - L2 (capability dispatch)
// Introduced: Win10 1607 (build 14393)
// ============================================================
COMPAT_API LPVOID WINAPI VirtualAlloc2(HANDLE Process, PVOID BaseAddress, SIZE_T RegionSize,
    DWORD AllocationType, DWORD Protect, MEM_EXTENDED_PARAMETER* ExtendedParameters, ULONG ParameterCount)
{
    // Try VirtualAllocExNuma for NUMA-aware allocations.
    // If no NUMA parameters, just use VirtualAlloc.
    if (!ExtendedParameters || ParameterCount == 0)
    {
        return VirtualAllocEx(Process, BaseAddress, RegionSize, AllocationType, Protect);
    }
    // Check if any NUMA parameter is present.
    BOOL hasNuma = FALSE;
    DWORD numaNode = 0;
    for (ULONG i = 0; i < ParameterCount; i++)
    {
        if (ExtendedParameters[i].Type == 1) // MemExtendedParameterNumaNode
        {
            hasNuma = TRUE;
            numaNode = (DWORD)(DWORD_PTR)ExtendedParameters[i].Pointer;
            break;
        }
    }
    if (hasNuma)
    {
        // VirtualAllocExNuma is available on Win10 1607+.
        typedef LPVOID (WINAPI *VirtualAllocExNuma_t)(HANDLE, PVOID, SIZE_T, DWORD, DWORD, DWORD);
        static VirtualAllocExNuma_t pFunc = NULL;
        if (!pFunc) pFunc = (VirtualAllocExNuma_t)GetProcAddress(GetModuleHandleA("kernel32.dll"), "VirtualAllocExNuma");
        if (pFunc) return pFunc(Process, BaseAddress, RegionSize, AllocationType, Protect, numaNode);
    }
    // Placeholder support not available; return NULL with error.
    if (AllocationType & MEM_RESERVE_PLACEHOLDER)
    {
        SetLastError(ERROR_NOT_SUPPORTED);
        return NULL;
    }
    return VirtualAllocEx(Process, BaseAddress, RegionSize, AllocationType, Protect);
}

// ============================================================
// MapViewOfFile3 - L2 (fallback to MapViewOfFile)
// Introduced: Win10 1803 (build 17134)
// ============================================================
COMPAT_API PVOID WINAPI MapViewOfFile3(HANDLE FileMapping, HANDLE Process, PVOID BaseAddress,
    ULONG64 Offset, SIZE_T ViewSize, ULONG AllocationType, DWORD Protect, MEM_EXTENDED_PARAMETER* ExtendedParameters, ULONG ParameterCount)
{
    (void)Process;
    (void)ExtendedParameters;
    (void)ParameterCount;
    // Most parameters map directly to MapViewOfFileEx; allocation type
    // and extended parameters are best-effort.
    if (AllocationType & (MEM_REPLACE_PLACEHOLDER | MEM_RESERVE_PLACEHOLDER))
    {
        SetLastError(ERROR_NOT_SUPPORTED);
        return NULL;
    }
    DWORD viewAccess = FILE_MAP_READ;
    if (Protect == PAGE_READWRITE) viewAccess = FILE_MAP_WRITE;
    else if (Protect == PAGE_EXECUTE_READ) viewAccess = FILE_MAP_EXECUTE;
    else if (Protect == PAGE_EXECUTE_READWRITE) viewAccess = FILE_MAP_ALL_ACCESS;
    DWORD offsetLow = (DWORD)(Offset & 0xFFFFFFFF);
    DWORD offsetHigh = (DWORD)(Offset >> 32);
    return MapViewOfFileEx(FileMapping, viewAccess, offsetLow, offsetHigh, ViewSize, BaseAddress);
}

// ============================================================
// VirtualProtectFromApp - L1 (fallback to VirtualProtect)
// Introduced: Win10 1607 (build 14393)
// ============================================================
COMPAT_API BOOL WINAPI VirtualProtectFromApp(PVOID lpAddress, SIZE_T dwSize, DWORD flNewProtect, PDWORD lpflOldProtect)
{
    return VirtualProtect(lpAddress, dwSize, flNewProtect, lpflOldProtect);
}

// ============================================================
// VirtualAllocFromApp - L1 (forward, fallback to VirtualAllocEx)
// Introduced: Win10 1607 (build 14393)
// ============================================================
COMPAT_API PVOID WINAPI VirtualAllocFromApp(PVOID BaseAddress, SIZE_T RegionSize, DWORD AllocationType, DWORD Protect)
{
    typedef PVOID(WINAPI *Fn)(PVOID, SIZE_T, DWORD, DWORD);
    static Fn pReal = nullptr;
    if (!pReal) pReal = (Fn)Compat_GetRealProc("kernel32", "VirtualAllocFromApp");
    if (pReal) return pReal(BaseAddress, RegionSize, AllocationType, Protect);
    return VirtualAllocEx(GetCurrentProcess(), BaseAddress, RegionSize, AllocationType, Protect);
}

// ============================================================
// MapViewOfFileFromApp - L1 (forward, fallback to MapViewOfFileEx)
// Introduced: Win10 1607 (build 14393)
// ============================================================
COMPAT_API PVOID WINAPI MapViewOfFileFromApp(HANDLE FileMapping, ULONG DesiredAccess, ULONG64 FileOffset, SIZE_T NumberOfBytesToMap)
{
    typedef PVOID(WINAPI *Fn)(HANDLE, ULONG, ULONG64, SIZE_T);
    static Fn pReal = nullptr;
    if (!pReal) pReal = (Fn)Compat_GetRealProc("kernel32", "MapViewOfFileFromApp");
    if (pReal) return pReal(FileMapping, DesiredAccess, FileOffset, NumberOfBytesToMap);
    return MapViewOfFileEx(FileMapping, DesiredAccess,
        (DWORD)(FileOffset & 0xFFFFFFFF), (DWORD)(FileOffset >> 32),
        NumberOfBytesToMap, NULL);
}

// ============================================================
// DiscardVirtualMemory - L3 (forward, benign stub)
// Introduced: Win10 1607 (build 14393)
// ============================================================
COMPAT_API DWORD WINAPI DiscardVirtualMemory(PVOID VirtualAddress, SIZE_T Size)
{
    typedef DWORD(WINAPI *Fn)(PVOID, SIZE_T);
    static Fn pReal = nullptr;
    if (!pReal) pReal = (Fn)Compat_GetRealProc("kernel32", "DiscardVirtualMemory");
    if (pReal) return pReal(VirtualAddress, Size);
    (void)VirtualAddress;
    (void)Size;
    return ERROR_SUCCESS;
}

// ============================================================
// OfferVirtualMemory - L3 (forward, benign stub)
// Introduced: Win10 1607 (build 14393)
// ============================================================
COMPAT_API DWORD WINAPI OfferVirtualMemory(PVOID VirtualAddress, SIZE_T Size, OFFER_PRIORITY Priority)
{
    typedef DWORD(WINAPI *Fn)(PVOID, SIZE_T, OFFER_PRIORITY);
    static Fn pReal = nullptr;
    if (!pReal) pReal = (Fn)Compat_GetRealProc("kernel32", "OfferVirtualMemory");
    if (pReal) return pReal(VirtualAddress, Size, Priority);
    (void)VirtualAddress;
    (void)Size;
    (void)Priority;
    return ERROR_SUCCESS;
}

// ============================================================
// ReclaimVirtualMemory - L3 (forward, benign stub)
// Introduced: Win10 1607 (build 14393)
// ============================================================
COMPAT_API DWORD WINAPI ReclaimVirtualMemory(const void* VirtualAddress, SIZE_T Size)
{
    typedef DWORD(WINAPI *Fn)(const void*, SIZE_T);
    static Fn pReal = nullptr;
    if (!pReal) pReal = (Fn)Compat_GetRealProc("kernel32", "ReclaimVirtualMemory");
    if (pReal) return pReal(VirtualAddress, Size);
    (void)VirtualAddress;
    (void)Size;
    return ERROR_SUCCESS;
}

// ============================================================
// PrefetchVirtualMemory - L3 (forward, benign stub)
// Introduced: Win10 1607 (build 14393)
// ============================================================
COMPAT_API BOOL WINAPI PrefetchVirtualMemory(HANDLE hProcess, ULONG_PTR NumberOfEntries, PWIN32_MEMORY_RANGE_ENTRY VirtualAddresses, ULONG Flags)
{
    typedef BOOL(WINAPI *Fn)(HANDLE, ULONG_PTR, PWIN32_MEMORY_RANGE_ENTRY, ULONG);
    static Fn pReal = nullptr;
    if (!pReal) pReal = (Fn)Compat_GetRealProc("kernel32", "PrefetchVirtualMemory");
    if (pReal) return pReal(hProcess, NumberOfEntries, VirtualAddresses, Flags);
    (void)hProcess;
    (void)NumberOfEntries;
    (void)VirtualAddresses;
    (void)Flags;
    return TRUE;
}
