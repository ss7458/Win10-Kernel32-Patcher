# Win10CompatRuntime

**Windows 10 Compatibility Runtime Framework**

A native C/C++ compatibility layer that provides missing Windows 10 APIs
for older systems (1507/1511/1607/1703/1709), enabling modern software
to run on legacy Windows 10 builds.

## Architecture

```
CompatPatch.exe          CompatRuntime.dll
       |                        |
  PE Parser +              API Implementations
  Import Redirect          (Thread, Memory, File,
       |                   Process, Time, Sync,
  Database                 DPI, DLL providers)
  (API -> Strategy)             |
       |                   Calls old APIs or
  Redirect to               implements new behavior
  CompatRuntime.dll
```

## Components

### CompatPatch.exe

Patches PE files to redirect missing API imports to CompatRuntime.dll.
Parses IMAGE_IMPORT_DESCRIPTOR and rebuilds import tables correctly
(no string replacement).

### CompatRuntime.dll

Exports Windows 10 APIs with the same names as the originals.
Organized by subsystem providers:

| Provider | APIs | Strategy |
|----------|------|----------|
| Thread | SetThreadDescription, GetThreadDescription, SetThreadInformation, GetThreadInformation | L0/L2 |
| Memory | VirtualAlloc2, MapViewOfFile3, VirtualProtectFromApp | L2/L1 |
| File | CreateFile2, GetFileInformationByHandleEx, GetFinalPathNameByHandleW | L1 |
| Process | QueryFullProcessImageNameW, GetPackageFamilyName, GetCurrentPackageId | L1/L3 |
| Time | GetSystemTimePreciseAsFileTime, QueryInterruptTimePrecise, GetTickCount64 | L1 |
| Sync | InitializeConditionVariable, WakeConditionVariable, SleepConditionVariableCS/SRW | L0 |
| DPI | GetDpiForWindow, SetProcessDpiAwarenessContext, EnableNonClientDpiScaling | L1/L3 |
| DLL | AddDllDirectory, RemoveDllDirectory, SetDefaultDllDirectories | L0 |

### Compatibility Levels

- **L0**: Full compatibility (100% functional)
- **L1**: Degraded (fallback to older API, slightly less functionality)
- **L2**: Capability dispatch (behavior depends on parameters)
- **L3**: Stub (returns success/benign data)
- **L4**: Not implementable (returns ERROR_CALL_NOT_IMPLEMENTED)

### Database

CSV files mapping APIs to compatibility strategies:
- `kernel32.csv` - Kernel32 APIs
- `kernelbase.csv` - KernelBase APIs
- `user32.csv` - User32 APIs
- `api-ms.csv` - API set virtual DLLs

## Building

### Prerequisites

- Windows 10 SDK
- CMake 3.20+
- Ninja or Visual Studio generator

### Build

```bash
cmake -S Win10CompatRuntime -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

### Run Tests

```bash
./build/bin/CompatTests.exe
```

## Usage

1. Build CompatRuntime.dll and CompatPatch.exe
2. Place CompatRuntime.dll in the same directory as the target program
3. Run: `CompatPatch.exe target.exe`
4. The patched file will be saved as `target.patched.exe`

## License

MIT License
