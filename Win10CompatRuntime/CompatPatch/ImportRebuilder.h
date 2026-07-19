// ImportRebuilder.h - Rebuilds PE import tables to redirect APIs.
//
// Strategy:
//   1. Collect all APIs to redirect (from database lookup).
//   2. Find or create a new import descriptor for "CompatRuntime.dll".
//   3. Append new IMAGE_THUNK_DATA + IMAGE_IMPORT_BY_NAME entries.
//   4. Remove redirected APIs from the original module descriptors.
//   5. Update the IMAGE_DIRECTORY_ENTRY_IMPORT size.

#pragma once
#include "PEUtils.h"
#include "ImportScanner.h"
#include <vector>
#include <string>

// Result of a patch operation.
struct PatchResult {
    int     apisRedirected;
    int     apisNotFound;
    int     apisAlreadyPatched;
    std::string errorMessage;
};

// Patch the import table of a PE file loaded into memory.
// redirectedAPIs: pairs of (moduleName, apiName) to redirect to CompatRuntime.dll.
// Returns the result with counts and any error message.
PatchResult PatchImports(
    std::vector<uint8_t>& fileData,
    const std::vector<std::pair<std::string, std::string>>& redirectedAPIs
);

// Write the patched data back to the same file (creates a backup first).
bool WritePatchedFile(const char* filePath, const std::vector<uint8_t>& data);
