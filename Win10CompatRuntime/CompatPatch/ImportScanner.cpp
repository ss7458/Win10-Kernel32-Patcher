// ImportScanner.cpp - Import table scanning implementation.

#include "ImportScanner.h"

std::vector<ImportEntry> ScanImports(void* fileBase, IMAGE_NT_HEADERS* nt)
{
    std::vector<ImportEntry> result;

    auto* dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (dir->VirtualAddress == 0 || dir->Size == 0) return result;

    auto* desc = pe::RvaPtr<IMAGE_IMPORT_DESCRIPTOR>(fileBase, nt, dir->VirtualAddress);
    if (!desc) return result;

    for (; desc->FirstThunk != 0; desc++)
    {
        const char* moduleName = pe::RvaPtr<char>(fileBase, nt, desc->Name);
        if (!moduleName) continue;

        // Walk OriginalFirstThunk (ILT) or FirstThunk (IAT) if ILT is absent.
        uint32_t oftRVA = desc->OriginalFirstThunk ? desc->OriginalFirstThunk : desc->FirstThunk;
        auto* oftEntry = pe::RvaPtr<IMAGE_THUNK_DATA>(fileBase, nt, oftRVA);
        auto* ftEntry  = pe::RvaPtr<IMAGE_THUNK_DATA>(fileBase, nt, desc->FirstThunk);
        if (!oftEntry || !ftEntry) continue;

        for (; oftEntry->u1.AddressOfData != 0; oftEntry++, ftEntry++)
        {
            ImportEntry ie;
            ie.moduleName = moduleName;
            // Store true RVAs (not file offsets) for the thunk locations.
            ie.thunkRVA = pe::FileOffsetToRva(nt, (uint32_t)((uint8_t*)ftEntry - (uint8_t*)fileBase));
            ie.origThunkRVA = pe::FileOffsetToRva(nt, (uint32_t)((uint8_t*)oftEntry - (uint8_t*)fileBase));

            if (IMAGE_SNAP_BY_ORDINAL(oftEntry->u1.Ordinal))
            {
                ie.isByName = false;
                ie.ordinal = (uint16_t)IMAGE_ORDINAL(oftEntry->u1.Ordinal);
                ie.apiName = "#" + std::to_string(ie.ordinal);
            }
            else
            {
                auto* hintName = pe::RvaPtr<IMAGE_IMPORT_BY_NAME>(fileBase, nt,
                    (uint32_t)oftEntry->u1.AddressOfData);
                if (!hintName) continue;
                ie.isByName = true;
                ie.ordinal = hintName->Hint;
                ie.apiName = (const char*)hintName->Name;
            }
            result.push_back(std::move(ie));
        }
    }
    return result;
}

std::vector<ImportEntry> ScanDelayImports(void* fileBase, IMAGE_NT_HEADERS* nt)
{
    std::vector<ImportEntry> result;

    auto* dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT];
    if (dir->VirtualAddress == 0 || dir->Size == 0) return result;

    auto* desc = pe::RvaPtr<IMAGE_DELAYLOAD_DESCRIPTOR>(fileBase, nt, dir->VirtualAddress);
    if (!desc) return result;

    for (; desc->DllNameRVA != 0; desc++)
    {
        const char* moduleName = pe::RvaPtr<char>(fileBase, nt, desc->DllNameRVA);
        if (!moduleName) continue;

        uint32_t oftRVA = desc->ImportNameTableRVA;
        uint32_t ftRVA = desc->ImportAddressTableRVA;
        auto* oftEntry = pe::RvaPtr<IMAGE_THUNK_DATA>(fileBase, nt, oftRVA);
        auto* ftEntry  = pe::RvaPtr<IMAGE_THUNK_DATA>(fileBase, nt, ftRVA);
        if (!oftEntry || !ftEntry) continue;

        for (; oftEntry->u1.AddressOfData != 0; oftEntry++, ftEntry++)
        {
            ImportEntry ie;
            ie.moduleName = moduleName;
            ie.thunkRVA = ftRVA + (uint32_t)((uint8_t*)ftEntry - (uint8_t*)fileBase);
            ie.origThunkRVA = oftRVA + (uint32_t)((uint8_t*)oftEntry - (uint8_t*)fileBase);

            if (IMAGE_SNAP_BY_ORDINAL(oftEntry->u1.Ordinal))
            {
                ie.isByName = false;
                ie.ordinal = (uint16_t)IMAGE_ORDINAL(oftEntry->u1.Ordinal);
                ie.apiName = "#" + std::to_string(ie.ordinal);
            }
            else
            {
                auto* hintName = pe::RvaPtr<IMAGE_IMPORT_BY_NAME>(fileBase, nt,
                    (uint32_t)oftEntry->u1.AddressOfData);
                if (!hintName) continue;
                ie.isByName = true;
                ie.ordinal = hintName->Hint;
                ie.apiName = (const char*)hintName->Name;
            }
            result.push_back(std::move(ie));
        }
    }
    return result;
}
