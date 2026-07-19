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
            numaNode = (DWORD)ExtendedParameters[i].Pointer.Value;
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
COMPAT_API LPVOID WINAPI MapViewOfFile3(HANDLE FileMapping, HANDLE Process, PVOID BaseAddress,
    SIZE_T RegionSize, ULONG AllocationType, DWORD Protect, MEM_EXTENDED_PARAMETER* ExtendedParameters, ULONG ParameterCount)
{
    // Most parameters map directly to MapViewOfFile; allocation type
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
    return MapViewOfFileEx(FileMapping, viewAccess, 0, 0, RegionSize, BaseAddress);
}

// ============================================================
// VirtualProtectFromApp - L1 (fallback to VirtualProtect)
// Introduced: Win10 1607 (build 14393)
// ============================================================
COMPAT_API BOOL WINAPI VirtualProtectFromApp(PVOID lpAddress, SIZE_T dwSize, DWORD flNewProtect, PDWORD lpflOldProtect)
{
    return VirtualProtect(lpAddress, dwSize, flNewProtect, lpflOldProtect);
}
