// main.cpp - CompatPatch CLI tool entry point.
//
// Usage: CompatPatch.exe <target.exe-or-dll> [--database <path>] [--dry-run]
//
// Reads the compatibility database, scans the target PE's import table,
// and redirects any matching APIs to CompatRuntime.dll.

#include "PEUtils.h"
#include "ImportScanner.h"
#include "ImportRebuilder.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>

struct ApiEntry {
    std::string module;
    std::string name;
    int introduced;
    std::string level;
    std::string strategy;
    std::string runtime;
};

// Simple JSON line parser for our flat database format.
// Expects lines like: "module","name",introduced,"level","strategy","runtime"
static std::vector<ApiEntry> LoadDatabase(const char* dbPath)
{
    std::vector<ApiEntry> entries;
    std::ifstream f(dbPath);
    if (!f.is_open()) return entries;

    std::string line;
    while (std::getline(f, line))
    {
        // Skip empty lines and comments.
        if (line.empty() || line[0] == '#') continue;

        // Parse CSV-like format: module,name,introduced,level,strategy,runtime
        ApiEntry e;
        std::istringstream ss(line);
        std::string token;

        if (!std::getline(ss, e.module, ',')) continue;
        if (!std::getline(ss, e.name, ',')) continue;
        if (!std::getline(ss, token, ',')) continue;
        e.introduced = std::stoi(token.empty() ? "0" : token);
        if (!std::getline(ss, e.level, ',')) continue;
        if (!std::getline(ss, e.strategy, ',')) continue;
        if (!std::getline(ss, e.runtime, ',')) continue;

        // Remove quotes.
        auto strip = [](std::string& s) {
            if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
                s = s.substr(1, s.size() - 2);
        };
        strip(e.module);
        strip(e.name);
        strip(e.level);
        strip(e.strategy);
        strip(e.runtime);

        entries.push_back(std::move(e));
    }
    return entries;
}

// Load all database files from a directory.
static std::vector<ApiEntry> LoadAllDatabases(const char* dbDir)
{
    std::vector<ApiEntry> all;
    const char* files[] = {
        "kernel32.csv", "kernelbase.csv", "user32.csv",
        "advapi32.csv", "api-ms.csv", nullptr
    };
    for (const char** f = files; *f; f++)
    {
        std::string path = std::string(dbDir) + "\\" + *f;
        auto entries = LoadDatabase(path.c_str());
        all.insert(all.end(), entries.begin(), entries.end());
    }
    return all;
}

static void PrintBanner()
{
    printf("========================================\n");
    printf(" CompatPatch - PE Import Redirect Tool\n");
    printf(" Windows 10 Compatibility Runtime\n");
    printf("========================================\n\n");
}

static void PrintUsage()
{
    printf("Usage: CompatPatch.exe <target.exe-or-dll> [options]\n\n");
    printf("Options:\n");
    printf("  --database <dir>  Path to API database directory (default: .\\Database)\n");
    printf("  --dry-run         Scan only, do not modify the file\n");
    printf("  --list            List all APIs that would be redirected\n");
    printf("  --help            Show this help\n");
}

// Resolve the database directory. If --database was passed, use it. Otherwise
// look for a "Database" folder next to this exe; fall back to ".\\Database"
// so the tool works with zero configuration.
static std::string ResolveDbDir(const char* userSpecified)
{
    if (userSpecified && userSpecified[0])
        return std::string(userSpecified);

    char exePath[MAX_PATH] = { 0 };
    if (GetModuleFileNameA(nullptr, exePath, MAX_PATH))
    {
        char* slash = strrchr(exePath, '\\');
        if (slash) *slash = '\0';
        std::string candidate = std::string(exePath) + "\\Database";
        DWORD attr = GetFileAttributesA(candidate.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
            return candidate;
    }
    return ".\\Database";
}

int main(int argc, char* argv[])
{
    PrintBanner();

    if (argc < 2)
    {
        PrintUsage();
        return 1;
    }

    const char* targetPath = nullptr;
    std::string dbDir = ResolveDbDir(nullptr);
    bool dryRun = false;
    bool listOnly = false;
    bool debug = false;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--help") == 0) { PrintUsage(); return 0; }
        else if (strcmp(argv[i], "--dry-run") == 0) dryRun = true;
        else if (strcmp(argv[i], "--list") == 0) listOnly = true;
        else if (strcmp(argv[i], "--debug") == 0) debug = true;
        else if (strcmp(argv[i], "--database") == 0 && i + 1 < argc) dbDir = argv[++i];
        else if (!targetPath) targetPath = argv[i];
    }

    if (!targetPath)
    {
        printf("[ERROR] No target file specified.\n");
        PrintUsage();
        return 1;
    }

    // Load database.
    printf("[INFO] Loading API database from: %s\n", dbDir.c_str());
    auto db = LoadAllDatabases(dbDir.c_str());
    printf("[INFO] Loaded %zu API entries.\n\n", db.size());

    // Read target file.
    printf("[INFO] Reading: %s\n", targetPath);
    auto fileData = pe::ReadFile(targetPath);
    if (fileData.empty())
    {
        printf("[ERROR] Cannot read file: %s\n", targetPath);
        return 1;
    }

    // Parse PE.
    if (fileData.size() < sizeof(IMAGE_DOS_HEADER))
    {
        printf("[ERROR] File too small.\n");
        return 1;
    }
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(fileData.data());
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
    {
        printf("[ERROR] Invalid DOS signature.\n");
        return 1;
    }
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(fileData.data() + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
    {
        printf("[ERROR] Invalid NT signature.\n");
        return 1;
    }
    printf("[INFO] PE parsed. Machine: 0x%X, Sections: %u\n\n",
        nt->FileHeader.Machine, nt->FileHeader.NumberOfSections);

    // Scan imports.
    auto imports = ScanImports(fileData.data(), nt);
    auto delayImports = ScanDelayImports(fileData.data(), nt);
    printf("[INFO] Found %zu import entries, %zu delay-import entries.\n\n",
        imports.size(), delayImports.size());

    // Build list of APIs to redirect.
    std::vector<std::pair<std::string, std::string>> toRedirect;
    for (auto& ie : imports)
    {
        for (auto& dbEntry : db)
        {
            if (ie.isByName && ie.apiName == dbEntry.name &&
                dbEntry.strategy == "Runtime")
            {
                // Case-insensitive module comparison.
                std::string ieMod = ie.moduleName;
                std::string dbMod = dbEntry.module;
                for (auto& c : ieMod) c = (char)tolower((unsigned char)c);
                for (auto& c : dbMod) c = (char)tolower((unsigned char)c);
                if (ieMod == dbMod || ieMod.find(dbMod) != std::string::npos)
                {
                bool found = false;
                for (auto& r : toRedirect)
                    if (r.first == ie.moduleName && r.second == ie.apiName)
                        { found = true; break; }
                if (!found)
                {
                    if (debug) printf("[DEBUG] match: %s!%s\n", ie.moduleName.c_str(), ie.apiName.c_str());
                    toRedirect.push_back({ie.moduleName, ie.apiName});
                }
                }
            }
        }
    }

    if (toRedirect.empty())
    {
        printf("[INFO] No APIs to redirect. File may already be compatible.\n");
        return 0;
    }

    printf("[INFO] APIs to redirect:\n");
    for (auto& r : toRedirect)
    {
        printf("  %s!%s -> CompatRuntime.dll!%s\n",
            r.first.c_str(), r.second.c_str(), r.second.c_str());
    }
    printf("\n");

    if (listOnly) return 0;

    if (dryRun)
    {
        printf("[DRY-RUN] No modifications made.\n");
        return 0;
    }

    // Patch.
    printf("[INFO] Patching import table...\n");
    auto result = PatchImports(fileData, toRedirect);

    if (!result.errorMessage.empty() && result.apisRedirected == 0)
    {
        printf("[ERROR] Patch failed: %s\n", result.errorMessage.c_str());
        return 1;
    }

    printf("[SUCCESS] Redirected %d API(s).\n", result.apisRedirected);

    // Write output: insert ".patched" before the extension so Windows still
    // recognizes the file as an executable (e.g. "app.exe" → "app.patched.exe").
    std::string target(targetPath);
    std::string outPath;
    size_t dotPos = target.rfind('.');
    if (dotPos != std::string::npos)
        outPath = target.substr(0, dotPos) + ".patched" + target.substr(dotPos);
    else
        outPath = target + ".patched";
    printf("[INFO] Writing: %s\n", outPath.c_str());
    if (!pe::WriteFile(outPath.c_str(), fileData))
    {
        printf("[ERROR] Cannot write output file.\n");
        return 1;
    }

    printf("[SUCCESS] Patched file saved. Replace original to apply.\n");
    return 0;
}
