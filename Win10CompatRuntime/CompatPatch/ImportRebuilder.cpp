// ImportRebuilder.cpp - Import table patching implementation.

#include "ImportRebuilder.h"
#include <cstdio>

static bool IsAPIRedirected(const std::string& module, const std::string& api,
    const std::vector<std::pair<std::string, std::string>>& list)
{
    for (auto& p : list)
    {
        // Case-insensitive comparison for module name, exact for API name.
        std::string modLower = module;
        for (auto& c : modLower) c = (char)tolower((unsigned char)c);
        std::string dbModLower = p.first;
        for (auto& c : dbModLower) c = (char)tolower((unsigned char)c);
        if (modLower == dbModLower && api == p.second) return true;
    }
    return false;
}

PatchResult PatchImports(
    std::vector<uint8_t>& fileData,
    const std::vector<std::pair<std::string, std::string>>& redirectedAPIs)
{
    PatchResult result = {};
    if (fileData.size() < sizeof(IMAGE_DOS_HEADER))
    {
        result.errorMessage = "File too small for DOS header";
        return result;
    }

    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(fileData.data());
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
    {
        result.errorMessage = "Invalid DOS signature";
        return result;
    }

    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(fileData.data() + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
    {
        result.errorMessage = "Invalid NT signature";
        return result;
    }

    // 1. Scan existing imports to find which APIs need redirection.
    auto imports = ScanImports(fileData.data(), nt);
    auto delayImports = ScanDelayImports(fileData.data(), nt);

    // Merge delay imports with a flag.
    struct FullImportEntry : ImportEntry { bool isDelay; };
    std::vector<FullImportEntry> allImports;
    for (auto& ie : imports) { FullImportEntry fi; fi = ie; fi.isDelay = false; allImports.push_back(fi); }
    for (auto& ie : delayImports) { FullImportEntry fi; fi = ie; fi.isDelay = true; allImports.push_back(fi); }

    // 2. Identify which imports to redirect.
    std::vector<FullImportEntry> toRedirect;
    for (auto& ie : allImports)
    {
        if (ie.isByName && IsAPIRedirected(ie.moduleName, ie.apiName, redirectedAPIs))
        {
            toRedirect.push_back(ie);
        }
    }

    if (toRedirect.empty())
    {
        result.errorMessage = "No matching APIs found in import table";
        return result;
    }

    // 3. Collect unique API names to redirect.
    std::vector<std::pair<std::string, std::string>> uniqueRedirects;
    for (auto& ie : toRedirect)
    {
        bool found = false;
        for (auto& ur : uniqueRedirects)
        {
            if (ur.first == ie.moduleName && ur.second == ie.apiName) { found = true; break; }
        }
        if (!found) uniqueRedirects.push_back({ie.moduleName, ie.apiName});
    }

    // 4. Find the import directory.
    auto* importDir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (importDir->VirtualAddress == 0 || importDir->Size == 0)
    {
        result.errorMessage = "No import directory found";
        return result;
    }

    // 5. Count existing descriptors.
    auto* origDesc = pe::RvaPtr<IMAGE_IMPORT_DESCRIPTOR>(fileData.data(), nt, importDir->VirtualAddress);
    if (!origDesc) { result.errorMessage = "Cannot read import descriptors"; return result; }

    int descCount = 0;
    for (auto* d = origDesc; d->FirstThunk != 0; d++) descCount++;

    // 6. Build new import data:
    //    - Modified descriptors (original minus redirected APIs)
    //    - New descriptor for CompatRuntime.dll
    //    - Thunk arrays for each original descriptor (with redirected APIs removed)
    //    - New thunk array + name strings for CompatRuntime
    //
    // For simplicity, we take a different approach:
    //   We append a new section ".compat" with:
    //   - One new IMAGE_IMPORT_DESCRIPTOR for "CompatRuntime.dll"
    //   - IMAGE_THUNK_DATA array for CompatRuntime's imports
    //   - IMAGE_IMPORT_BY_NAME entries for each redirected API
    //   - The DLL name string "CompatRuntime.dll"
    //   - API name strings
    //
    // We also update the original descriptors to NULL out the redirected
    // thunks (so they don't cause "entry point not found" errors).
    //
    // Actually, the cleanest approach: just append a new section with new
    // descriptors and update the data directory to encompass both old and new.

    // Calculate sizes for new section content.
    const char* compatDllName = "CompatRuntime.dll";
    size_t dllNameLen = strlen(compatDllName) + 1;

    // Each redirected API needs: IMAGE_IMPORT_BY_NAME (2 + name_len + 1) + IMAGE_THUNK_DATA
    size_t namesSize = 0;
    std::vector<size_t> nameOffsets; // offsets within the name area
    for (auto& ur : uniqueRedirects)
    {
        nameOffsets.push_back(namesSize);
        namesSize += 2 + ur.second.size() + 1; // Hint(2) + name + null
    }

    // Layout of new section content (offsets relative to section start):
    // [0 .. descCount]           : IMAGE_IMPORT_DESCRIPTOR array (original descriptors, modified)
    // [descCount .. descCount+1] : New IMAGE_IMPORT_DESCRIPTOR for CompatRuntime
    // [descCount+1 .. 0]        : End sentinel (zero descriptor)
    // Then: IMAGE_THUNK_DATA arrays for CompatRuntime
    // Then: IMAGE_IMPORT_BY_NAME entries
    // Then: DLL name string

    int totalDescs = descCount + 1 + 1; // original + CompatRuntime + end sentinel
    size_t descArraySize = totalDescs * sizeof(IMAGE_IMPORT_DESCRIPTOR);

    // Align up.
    uint32_t align = nt->OptionalHeader.SectionAlignment;
    if (align == 0) align = 4096;

    // Calculate total content size.
    size_t contentSize = 0;
    contentSize = pe::AlignUp((uint32_t)descArraySize, align); // descriptors
    size_t thunkAreaOffset = contentSize;
    size_t thunkAreaSize = (uniqueRedirects.size() + 1) * sizeof(IMAGE_THUNK_DATA); // +1 for null terminator
    contentSize += pe::AlignUp((uint32_t)thunkAreaSize, align);
    size_t nameAreaOffset = contentSize;
    contentSize += pe::AlignUp((uint32_t)namesSize, align);
    size_t dllNameOffset = contentSize;
    contentSize += pe::AlignUp((uint32_t)dllNameLen, align);
    size_t totalSectionSize = pe::AlignUp((uint32_t)contentSize, align);
    size_t totalRawSize = pe::AlignUp((uint32_t)contentSize, nt->OptionalHeader.FileAlignment);

    // Find where to place the new section.
    IMAGE_SECTION_HEADER* secTable = IMAGE_FIRST_SECTION(nt);
    IMAGE_SECTION_HEADER* lastSec = &secTable[nt->FileHeader.NumberOfSections - 1];

    // Calculate new section's RVA and file offset.
    uint32_t newSecVA = pe::AlignUp(lastSec->VirtualAddress + lastSec->Misc.VirtualSize, align);
    uint32_t newSecFileOffset = pe::AlignUp(lastSec->PointerToRawData + lastSec->SizeOfRawData,
        nt->OptionalHeader.FileAlignment);
    if (newSecFileOffset == 0) newSecFileOffset = pe::AlignUp((uint32_t)fileData.size(),
        nt->OptionalHeader.FileAlignment);

    // Expand the file if needed.
    size_t newFileSize = newSecFileOffset + totalRawSize;
    if (fileData.size() < newFileSize) fileData.resize(newFileSize, 0);

    // Zero out the new section area.
    memset(fileData.data() + newSecFileOffset, 0, totalRawSize);

    // 7. Write the new section content at newSecFileOffset.
    uint8_t* secBase = fileData.data() + newSecFileOffset;

    // 7a. Write modified original descriptors.
    //     For each descriptor, we keep it but null out the thunks that are
    //     being redirected. We do this by scanning and patching.
    //     Actually, a simpler approach: write all original descriptors as-is,
    //     then after writing the new section, patch the IAT thunks to point
    //     to the new CompatRuntime thunks.
    //
    //     Even simpler: don't modify original descriptors. Instead, just add
    //     a new descriptor for CompatRuntime with the same API names.
    //     The loader will try the original first (fails) then the new one.
    //     But actually, if kernel32 doesn't export the API, the import
    //     descriptor for kernel32 will fail to load entirely.
    //
    //     So we MUST remove the redirected APIs from the original descriptor.
    //     The cleanest way: write a NEW set of descriptors and thunks.

    // Write the new descriptor array: original descriptors (but with redirected
    // APIs' thunks nulled) + CompatRuntime descriptor + end sentinel.

    // For each original descriptor, we need to rebuild its thunk array
    // without the redirected APIs. This requires:
    //   - Scanning OriginalFirstThunk for each descriptor
    //   - Copying non-redirected thunks
    //   - Creating new thunk arrays in our section

    // This is complex. Let's use a simpler approach:
    // Write all original descriptors pointing to new thunk arrays in our section.
    // For each descriptor, copy non-redirected thunks.

    // Calculate how many non-redirected thunks per original descriptor.
    std::vector<std::vector<IMAGE_THUNK_DATA>> newThunkArrays(descCount);
    std::vector<uint32_t> newThunkRVA(descCount);

    for (int d = 0; d < descCount; d++)
    {
        auto* desc = &origDesc[d];
        uint32_t oftRVA = desc->OriginalFirstThunk ? desc->OriginalFirstThunk : desc->FirstThunk;
        auto* oftEntry = pe::RvaPtr<IMAGE_THUNK_DATA>(fileData.data(), nt, oftRVA);
        if (!oftEntry) continue;

        for (; oftEntry->u1.AddressOfData != 0; oftEntry++)
        {
            bool shouldRedirect = false;
            if (!IMAGE_SNAP_BY_ORDINAL(oftEntry->u1.Ordinal))
            {
                auto* hintName = pe::RvaPtr<IMAGE_IMPORT_BY_NAME>(fileData.data(), nt,
                    (uint32_t)oftEntry->u1.AddressOfData);
                if (hintName)
                {
                    std::string apiName = (const char*)hintName->Name;
                    if (IsAPIRedirected(desc->Name ?
                        pe::RvaPtr<char>(fileData.data(), nt, desc->Name) : "",
                        apiName, redirectedAPIs))
                    {
                        shouldRedirect = true;
                    }
                }
            }
            if (!shouldRedirect)
            {
                newThunkArrays[d].push_back(*oftEntry);
            }
        }
    }

    // Now write everything to the new section.
    size_t writeOffset = 0;

    // Write descriptor array (will be updated with RVAs later).
    size_t descArrayFileOffset = newSecFileOffset;
    // Total: original descriptors (modified) + CompatRuntime + end sentinel
    int totalNewDescs = descCount + 2;

    // Calculate where thunks go.
    size_t thunkStartOffset = pe::AlignUp((uint32_t)(totalNewDescs * sizeof(IMAGE_IMPORT_DESCRIPTOR)), (uint32_t)nt->OptionalHeader.SectionAlignment);

    // Assign RVA for each thunk array.
    uint32_t currentThunkRVA = newSecVA + (uint32_t)thunkStartOffset;
    std::vector<uint32_t> origThunkNewRVA(descCount);
    for (int d = 0; d < descCount; d++)
    {
        origThunkNewRVA[d] = currentThunkRVA;
        currentThunkRVA += (uint32_t)((newThunkArrays[d].size() + 1) * sizeof(IMAGE_THUNK_DATA));
    }
    uint32_t compatThunkRVA = currentThunkRVA;
    currentThunkRVA += (uint32_t)((uniqueRedirects.size() + 1) * sizeof(IMAGE_THUNK_DATA));

    // Name area starts after thunks.
    size_t nameStartOffset = (size_t)(currentThunkRVA - newSecVA);

    // Write descriptor array.
    for (int d = 0; d < descCount; d++)
    {
        IMAGE_IMPORT_DESCRIPTOR newDesc = origDesc[d];
        newDesc.OriginalFirstThunk = origThunkNewRVA[d];
        newDesc.FirstThunk = origThunkNewRVA[d]; // IAT same as ILT for simplicity
        memcpy(secBase + d * sizeof(IMAGE_IMPORT_DESCRIPTOR), &newDesc, sizeof(newDesc));
    }

    // CompatRuntime descriptor.
    IMAGE_IMPORT_DESCRIPTOR compatDesc = {};
    compatDesc.OriginalFirstThunk = compatThunkRVA;
    compatDesc.FirstThunk = compatThunkRVA;
    compatDesc.Name = newSecVA + (uint32_t)dllNameOffset;
    memcpy(secBase + descCount * sizeof(IMAGE_IMPORT_DESCRIPTOR), &compatDesc, sizeof(compatDesc));

    // End sentinel.
    IMAGE_IMPORT_DESCRIPTOR endDesc = {};
    memcpy(secBase + (descCount + 1) * sizeof(IMAGE_IMPORT_DESCRIPTOR), &endDesc, sizeof(endDesc));

    // Write original thunks (modified, without redirected APIs).
    for (int d = 0; d < descCount; d++)
    {
        size_t thunkFileOffset = newSecFileOffset + thunkStartOffset;
        for (int t = 0; t < descCount; t++)
        {
            if (t == d)
            {
                // Write this descriptor's thunks.
                size_t offset = thunkFileOffset;
                for (auto& thunk : newThunkArrays[t])
                {
                    memcpy(secBase + (offset - newSecFileOffset), &thunk, sizeof(thunk));
                    offset += sizeof(IMAGE_THUNK_DATA);
                }
                // Null terminator.
                IMAGE_THUNK_DATA nullThunk = {};
                memcpy(secBase + (offset - newSecFileOffset), &nullThunk, sizeof(nullThunk));
                offset += sizeof(IMAGE_THUNK_DATA);
                thunkFileOffset = offset;
            }
            else
            {
                thunkFileOffset += (newThunkArrays[t].size() + 1) * sizeof(IMAGE_THUNK_DATA);
            }
        }
    }

    // Write CompatRuntime thunks (the redirected APIs).
    size_t compatThunkFileOffset = newSecFileOffset + (size_t)(compatThunkRVA - newSecVA);
    size_t nameFileOffset = newSecFileOffset + nameStartOffset;

    for (size_t i = 0; i < uniqueRedirects.size(); i++)
    {
        IMAGE_THUNK_DATA thunk = {};
        thunk.u1.AddressOfData = newSecVA + (uint32_t)(nameStartOffset + nameOffsets[i]);
        memcpy(secBase + (compatThunkFileOffset - newSecFileOffset) + i * sizeof(IMAGE_THUNK_DATA),
            &thunk, sizeof(thunk));

        // Write IMAGE_IMPORT_BY_NAME.
        auto& ur = uniqueRedirects[i];
        uint8_t* namePtr = secBase + nameStartOffset + nameOffsets[i];
        uint16_t hint = 0; // Hint can be 0 for forwarded imports
        memcpy(namePtr, &hint, 2);
        memcpy(namePtr + 2, ur.second.c_str(), ur.second.size() + 1);
    }

    // Null terminator for CompatRuntime thunks.
    {
        IMAGE_THUNK_DATA nullThunk = {};
        memcpy(secBase + (compatThunkFileOffset - newSecFileOffset) +
            uniqueRedirects.size() * sizeof(IMAGE_THUNK_DATA), &nullThunk, sizeof(nullThunk));
    }

    // Write DLL name.
    memcpy(secBase + dllNameOffset, compatDllName, dllNameLen);

    // 8. Add the new section header.
    IMAGE_SECTION_HEADER newSec = {};
    memcpy(newSec.Name, ".compat", 8);
    newSec.Misc.VirtualSize = (DWORD)contentSize;
    newSec.VirtualAddress = newSecVA;
    newSec.SizeOfRawData = (DWORD)totalRawSize;
    newSec.PointerToRawData = newSecFileOffset;
    newSec.Characteristics = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ;

    // Add section header after existing ones.
    memcpy(&secTable[nt->FileHeader.NumberOfSections], &newSec, sizeof(newSec));
    nt->FileHeader.NumberOfSections++;

    // 9. Update the import directory to encompass the new section.
    //    We extend the import directory to cover both old and new descriptors.
    importDir->Size += (DWORD)(2 * sizeof(IMAGE_IMPORT_DESCRIPTOR)); // CompatRuntime + sentinel

    // 10. Update SizeOfImage.
    nt->OptionalHeader.SizeOfImage = newSecVA + (DWORD)totalSectionSize;

    result.apisRedirected = (int)uniqueRedirects.size();
    return result;
}

bool WritePatchedFile(const char* filePath, const std::vector<uint8_t>& data)
{
    // Create backup.
    std::string backupPath = std::string(filePath) + ".bak";
    // Copy original to backup (if backup doesn't exist).
    FILE* f = nullptr;
    fopen_s(&f, backupPath.c_str(), "rb");
    if (!f)
    {
        // No backup exists; copy original.
        auto orig = pe::ReadFile(filePath);
        if (!orig.empty()) pe::WriteFile(backupPath.c_str(), orig);
    }
    else
    {
        fclose(f);
    }
    return pe::WriteFile(filePath, data);
}
