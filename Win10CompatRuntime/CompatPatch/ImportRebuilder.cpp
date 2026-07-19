// ImportRebuilder.cpp - Import table patching implementation.
//
// Correct redirection strategy (preserves IAT locations, works for any number
// of APIs across any modules):
//   For each redirected API we create ONE new IMAGE_IMPORT_DESCRIPTOR for
//   "CompatRuntime.dll". Its FirstThunk points DIRECTLY at the original IAT
//   slot of that API, and its OriginalFirstThunk points at a private 1-entry
//   ILT that names the API. The Windows loader then resolves the API from
//   CompatRuntime.dll and writes the address into the SAME original IAT slot.
//   Because the program's call sites reference the original IAT addresses, no
//   code modification is needed.
//
//   To stop the loader from also resolving the API from the original module
//   (which would fail on an older OS), we null out both the ILT and IAT
//   thunks of the original descriptor entry.

#include "ImportRebuilder.h"
#include <cstdio>
#include <vector>
#include <string>
#include <algorithm>
#include <cstring>

static bool IsAPIRedirected(const std::string& module, const std::string& api,
    const std::vector<std::pair<std::string, std::string>>& list)
{
    std::string modLower = module;
    for (auto& c : modLower) c = (char)tolower((unsigned char)c);
    for (auto& p : list)
    {
        std::string dbModLower = p.first;
        for (auto& c : dbModLower) c = (char)tolower((unsigned char)c);
        if (modLower == dbModLower && api == p.second) return true;
        if (modLower.find(dbModLower) != std::string::npos && api == p.second) return true;
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

    auto* importDir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (importDir->VirtualAddress == 0 || importDir->Size == 0)
    {
        result.errorMessage = "No import directory found";
        return result;
    }

    // 1. Scan imports to find redirected APIs and their IAT/ILT locations.
    auto imports = ScanImports(fileData.data(), nt);

    struct RedirectTarget {
        uint32_t iatRVA;     // RVA of the original IAT entry (loader writes here)
        uint32_t iatFileOff;  // file offset of the original IAT entry (to null it)
        uint32_t iltFileOff;  // file offset of the original ILT entry (to null it)
        std::string apiName;
    };
    std::vector<RedirectTarget> targets;

    for (auto& ie : imports)
    {
        if (ie.isByName && IsAPIRedirected(ie.moduleName, ie.apiName, redirectedAPIs))
        {
            RedirectTarget t;
            t.iatRVA = ie.thunkRVA;
            t.iatFileOff = pe::RvaToFileOffset(nt, ie.thunkRVA);
            t.iltFileOff = pe::RvaToFileOffset(nt, ie.origThunkRVA);
            t.apiName = ie.apiName;
            targets.push_back(t);
        }
    }

    if (targets.empty())
    {
        result.errorMessage = "No matching APIs found in import table";
        return result;
    }

    // Locate the original import descriptor array (used by step 2 and step 3).
    auto* origDesc = pe::RvaPtr<IMAGE_IMPORT_DESCRIPTOR>(fileData.data(), nt, importDir->VirtualAddress);
    if (!origDesc)
    {
        result.errorMessage = "Cannot locate import descriptor array";
        return result;
    }

    // 2. Retarget each original ILT entry to a "decoy" name (an API the
    //    original module always exports, e.g. that descriptor's first import).
    //    We must NOT zero the entry: a zero ILT entry in the middle of a
    //    descriptor truncates the loader's import walk and leaves later IAT
    //    slots (e.g. GetCurrentThreadId) unresolved -> crash. By pointing the
    //    entry at a valid decoy, the loader still resolves something from the
    //    original module on EVERY OS, then the CompatRuntime descriptor
    //    (processed after, same IAT slot) overwrites it with the real
    //    compatibility implementation.
    for (auto& t : targets)
    {
        // Locate the descriptor that owns this IAT slot.
        IMAGE_IMPORT_DESCRIPTOR* owner = nullptr;
        for (auto* d = origDesc; d->FirstThunk != 0; d++)
        {
            uint32_t oftRVA = d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk;
            auto* oftEntry = pe::RvaPtr<IMAGE_THUNK_DATA>(fileData.data(), nt, oftRVA);
            auto* ftEntry = pe::RvaPtr<IMAGE_THUNK_DATA>(fileData.data(), nt, d->FirstThunk);
            if (!oftEntry || !ftEntry) continue;
            for (; oftEntry->u1.AddressOfData != 0; oftEntry++, ftEntry++)
            {
                if ((uint8_t*)ftEntry - fileData.data() == t.iatFileOff)
                {
                    owner = d;
                    break;
                }
            }
            if (owner) break;
        }
        if (!owner) continue;

        // Decoy: reuse the owner descriptor's first ILT name (a valid export
        // of the original module on all supported OSes).
        uint32_t decoyOftRVA = owner->OriginalFirstThunk ? owner->OriginalFirstThunk : owner->FirstThunk;
        auto* decoyOft = pe::RvaPtr<IMAGE_THUNK_DATA>(fileData.data(), nt, decoyOftRVA);
        if (!decoyOft || decoyOft->u1.AddressOfData == 0) continue;
        uint32_t decoyNameRVA = (uint32_t)decoyOft->u1.AddressOfData;

        if (t.iltFileOff)
        {
            auto* oft = reinterpret_cast<IMAGE_THUNK_DATA*>(fileData.data() + t.iltFileOff);
            oft->u1.AddressOfData = decoyNameRVA;
        }
    }

    // 3. Build the new section content.
    const char* compatDllName = "CompatRuntime.dll";
    size_t dllNameLen = strlen(compatDllName) + 1;

    uint32_t align = nt->OptionalHeader.SectionAlignment;
    if (align == 0) align = 4096;
    uint32_t fileAlign = nt->OptionalHeader.FileAlignment;
    if (fileAlign == 0) fileAlign = 512;

    size_t n = targets.size();

    // Count original descriptors (origDesc already located before step 2).
    int descCount = 0;
    for (auto* d = origDesc; d->FirstThunk != 0; d++) descCount++;

    // Layout (offsets relative to new section start):
    //   offDescArray : original descs + n compat descs + sentinel
    //   offIltArrays : n private ILT arrays, each [1 entry][null] (16 bytes)
    //   offNameArea  : n IMAGE_IMPORT_BY_NAME entries
    //   offDllName   : "CompatRuntime.dll" (shared by all compat descs)
    size_t offDescArray = 0;
    size_t descArraySize = (descCount + (size_t)n + 1) * sizeof(IMAGE_IMPORT_DESCRIPTOR);
    size_t offIltArrays = pe::AlignUp((uint32_t)(offDescArray + descArraySize), 16);
    // Each compat descriptor gets its own 2-thunk ILT (1 entry + null terminator).
    size_t iltArraysSize = n * 2 * sizeof(IMAGE_THUNK_DATA);
    size_t offNameArea = pe::AlignUp((uint32_t)(offIltArrays + iltArraysSize), 16);

    size_t nameAreaSize = 0;
    std::vector<size_t> nameOffsets(n);
    for (size_t i = 0; i < n; i++)
    {
        nameOffsets[i] = nameAreaSize;
        nameAreaSize += 2 + targets[i].apiName.size() + 1; // hint(2) + name + NUL
    }
    size_t offDllName = pe::AlignUp((uint32_t)(offNameArea + nameAreaSize), 16);
    size_t totalContent = pe::AlignUp((uint32_t)(offDllName + dllNameLen), 16);
    size_t totalRaw = pe::AlignUp((uint32_t)totalContent, fileAlign);

    // Place new section after the last existing section.
    IMAGE_SECTION_HEADER* secTable = IMAGE_FIRST_SECTION(nt);
    IMAGE_SECTION_HEADER* lastSec = &secTable[nt->FileHeader.NumberOfSections - 1];
    uint32_t newSecVA = pe::AlignUp(lastSec->VirtualAddress + lastSec->Misc.VirtualSize, align);
    uint32_t newSecFileOffset = pe::AlignUp(lastSec->PointerToRawData + lastSec->SizeOfRawData, fileAlign);
    if (newSecFileOffset == 0) newSecFileOffset = pe::AlignUp((uint32_t)fileData.size(), fileAlign);

    size_t newFileSize = newSecFileOffset + totalRaw;
    if (fileData.size() < newFileSize) fileData.resize(newFileSize, 0);
    uint8_t* secBase = fileData.data() + newSecFileOffset;
    memset(secBase, 0, totalRaw);

    // CRITICAL: resize() above may have reallocated the underlying buffer,
    // invalidating every PE pointer obtained before it (nt, importDir,
    // secTable, origDesc). Re-derive them from the (possibly new) buffer.
    nt = reinterpret_cast<IMAGE_NT_HEADERS*>(fileData.data() + dos->e_lfanew);
    importDir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    secTable = IMAGE_FIRST_SECTION(nt);
    origDesc = pe::RvaPtr<IMAGE_IMPORT_DESCRIPTOR>(fileData.data(), nt, importDir->VirtualAddress);

    // Write original descriptors (unmodified) into the new descriptor array.
    for (int d = 0; d < descCount; d++)
    {
        memcpy(secBase + offDescArray + d * sizeof(IMAGE_IMPORT_DESCRIPTOR),
               &origDesc[d], sizeof(IMAGE_IMPORT_DESCRIPTOR));
    }

    // Write one CompatRuntime descriptor per redirected API.
    for (size_t i = 0; i < n; i++)
    {
        IMAGE_IMPORT_DESCRIPTOR compatDesc = {};
        compatDesc.OriginalFirstThunk = (uint32_t)(newSecVA + offIltArrays + i * 2 * sizeof(IMAGE_THUNK_DATA));
        compatDesc.FirstThunk = targets[i].iatRVA; // loader writes into original IAT slot
        compatDesc.Name = (uint32_t)(newSecVA + offDllName);
        memcpy(secBase + offDescArray + (descCount + i) * sizeof(IMAGE_IMPORT_DESCRIPTOR),
               &compatDesc, sizeof(compatDesc));
    }

    // Write sentinel descriptor.
    {
        IMAGE_IMPORT_DESCRIPTOR sentinel = {};
        memcpy(secBase + offDescArray + (descCount + n) * sizeof(IMAGE_IMPORT_DESCRIPTOR),
               &sentinel, sizeof(sentinel));
    }

    // Write private ILT arrays: each [1 entry -> IMAGE_IMPORT_BY_NAME][null].
    for (size_t i = 0; i < n; i++)
    {
        uint8_t* iltBase = secBase + offIltArrays + i * 2 * sizeof(IMAGE_THUNK_DATA);
        IMAGE_THUNK_DATA entry = {};
        entry.u1.AddressOfData = (uint32_t)(newSecVA + offNameArea + nameOffsets[i]);
        memcpy(iltBase, &entry, sizeof(entry));
        IMAGE_THUNK_DATA nullThunk = {};
        memcpy(iltBase + sizeof(IMAGE_THUNK_DATA), &nullThunk, sizeof(nullThunk));
    }

    // Write IMAGE_IMPORT_BY_NAME entries (hint + name).
    for (size_t i = 0; i < n; i++)
    {
        uint8_t* namePtr = secBase + offNameArea + nameOffsets[i];
        uint16_t hint = 0;
        memcpy(namePtr, &hint, 2);
        memcpy(namePtr + 2, targets[i].apiName.c_str(), targets[i].apiName.size() + 1);
    }

    // Write DLL name.
    memcpy(secBase + offDllName, compatDllName, dllNameLen);

    // 4. Add the new section header.
    IMAGE_SECTION_HEADER newSec = {};
    memcpy(newSec.Name, ".compat", 8);
    newSec.Misc.VirtualSize = (DWORD)totalContent;
    newSec.VirtualAddress = newSecVA;
    newSec.SizeOfRawData = (DWORD)totalRaw;
    newSec.PointerToRawData = newSecFileOffset;
    newSec.Characteristics = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ;
    memcpy(&secTable[nt->FileHeader.NumberOfSections], &newSec, sizeof(newSec));
    nt->FileHeader.NumberOfSections++;

    // 5. Update the import directory to point to the new descriptor array.
    importDir->VirtualAddress = (uint32_t)(newSecVA + offDescArray);
    importDir->Size = (DWORD)((descCount + n + 1) * sizeof(IMAGE_IMPORT_DESCRIPTOR));

    // 6. Update SizeOfImage.
    nt->OptionalHeader.SizeOfImage = pe::AlignUp(newSecVA + (DWORD)totalContent, align);

    result.apisRedirected = (int)n;
    return result;
}

bool WritePatchedFile(const char* filePath, const std::vector<uint8_t>& data)
{
    std::string backupPath = std::string(filePath) + ".bak";
    FILE* f = nullptr;
    fopen_s(&f, backupPath.c_str(), "rb");
    if (!f)
    {
        auto orig = pe::ReadFile(filePath);
        if (!orig.empty()) pe::WriteFile(backupPath.c_str(), orig);
    }
    else
    {
        fclose(f);
    }
    return pe::WriteFile(filePath, data);
}
