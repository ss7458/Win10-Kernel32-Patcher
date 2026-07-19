// PEUtils.h - Portable Executable format utilities.
// Provides lightweight parsing of PE headers, section tables,
// and data directories without external dependencies.

#pragma once

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winnt.h>
#endif

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>

namespace pe {

// Read a file into memory. Returns empty vector on failure.
inline std::vector<uint8_t> ReadFile(const char* path)
{
    FILE* f = nullptr;
    fopen_s(&f, path, "rb");
    if (!f) return {};
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return {}; }
    std::vector<uint8_t> buf((size_t)sz);
    size_t read = fread(buf.data(), 1, (size_t)sz, f);
    fclose(f);
    buf.resize(read);
    return buf;
}

// Write a buffer to a file. Returns true on success.
inline bool WriteFile(const char* path, const std::vector<uint8_t>& data)
{
    FILE* f = nullptr;
    fopen_s(&f, path, "wb");
    if (!f) return false;
    size_t written = fwrite(data.data(), 1, data.size(), f);
    fclose(f);
    return written == data.size();
}

// Helper: convert an RVA to a file offset given the section table.
inline uint32_t RvaToFileOffset(IMAGE_NT_HEADERS* nt, uint32_t rva)
{
    IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++)
    {
        if (rva >= sec[i].VirtualAddress &&
            rva < sec[i].VirtualAddress + sec[i].Misc.VirtualSize)
        {
            return (rva - sec[i].VirtualAddress) + sec[i].PointerToRawData;
        }
    }
    return 0;
}

// Get a pointer into the loaded image buffer at an RVA.
template<typename T>
inline T* RvaPtr(void* base, IMAGE_NT_HEADERS* nt, uint32_t rva)
{
    uint32_t offset = RvaToFileOffset(nt, rva);
    if (offset == 0) return nullptr;
    return reinterpret_cast<T*>(static_cast<uint8_t*>(base) + offset);
}

// Find the section that has the most free space (for appending data).
inline IMAGE_SECTION_HEADER* FindFreeSection(IMAGE_NT_HEADERS* nt)
{
    IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    IMAGE_SECTION_HEADER* best = nullptr;
    DWORD bestFree = 0;
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++)
    {
        // Free space = VirtualSize - SizeOfRawData (if section has raw data)
        DWORD used = sec[i].SizeOfRawData;
        DWORD avail = sec[i].Misc.VirtualSize;
        if (avail > used)
        {
            DWORD freeBytes = avail - used;
            if (freeBytes > bestFree)
            {
                bestFree = freeBytes;
                best = &sec[i];
            }
        }
    }
    return best;
}

// Align a value up to the given alignment (power of two).
inline uint32_t AlignUp(uint32_t value, uint32_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

} // namespace pe
