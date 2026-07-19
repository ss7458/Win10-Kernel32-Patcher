// ImportScanner.h - Scans PE import tables and delay-import tables.
// Identifies which imported APIs are from modules we can patch
// and which specific API names need redirection.

#pragma once
#include "PEUtils.h"
#include <vector>
#include <string>

struct ImportEntry {
    std::string moduleName;      // e.g. "kernel32.dll"
    std::string apiName;         // e.g. "GetThreadDescription"
    uint16_t    ordinal;         // 0 if by name, nonzero if by ordinal
    uint32_t    thunkRVA;        // RVA of the IMAGE_THUNK_DATA in the IAT
    uint32_t    origThunkRVA;    // RVA of the IMAGE_THUNK_DATA in the ILT
    bool        isByName;        // true if imported by name
};

// Scan the import directory of a loaded PE image.
// Returns all imported API entries.
std::vector<ImportEntry> ScanImports(void* fileBase, IMAGE_NT_HEADERS* nt);

// Scan the delay-import directory.
std::vector<ImportEntry> ScanDelayImports(void* fileBase, IMAGE_NT_HEADERS* nt);
