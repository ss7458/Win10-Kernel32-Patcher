# Win10CompatRuntime 项目交接文档

> 生成时间：2026-07-23
> 项目路径：`c:\Users\Administrator\Downloads\opencode\new_opencode\cxx-runtime`

---

## 一、项目概述

### 1.1 项目目标

Win10CompatRuntime 是一个 Windows 10 兼容运行时框架，解决的核心问题是：**用新版 Windows 10 SDK 编译的程序，在旧版 Windows 10（1507/1511/1607/1703/1709）上因缺少 API 而无法启动**。

典型报错：`无法定位程序输入点 ClosePseudoConsole 于动态链接库`

### 1.2 工作原理

```
目标 EXE 导入了新版 API（如 ClosePseudoConsole）
  │
  ├─ 方案 A：静态修补（CompatPatch.exe）
  │    ├─ 读取 Database/*.csv 获取需重定向的 API 清单
  │    ├─ 扫描目标 PE 的导入表（标准导入 + 延迟导入）
  │    ├─ 添加 .compat 节，重建导入描述符
  │    │    每个 API 一个 CompatRuntime.dll 描述符
  │    │    FirstThunk 指向原始 IAT 槽位（loader 直接覆盖）
  │    │    原始 ILT 条目改为 decoy 名（防止截断导入遍历）
  │    └─ 输出 .patched 文件
  │
  └─ 方案 B：运行时注入（CompatLoader.exe）
       ├─ CreateProcess(SUSPENDED) 挂起目标
       ├─ VirtualAllocEx + WriteProcessMemory 写入 DLL 路径
       ├─ CreateRemoteThread(LoadLibraryA) 注入 CompatRuntime.dll
       ├─ DllMain 检测 COMPAT_LOADER 环境变量
       │    └─ RuntimePatch.cpp 重写宿主 EXE 的 IAT
       └─ ResumeThread 恢复目标
```

### 1.3 兼容级别体系

| 级别 | 含义 | 行为 |
|------|------|------|
| L0 | 完全兼容 | 100% 功能等价实现 |
| L1 | 降级兼容 | 使用旧版 API 模拟，功能略有缺失 |
| L2 | 能力分派 | 根据参数决定行为（部分参数不支持） |
| L3 | 桩实现 | 返回成功/良性假数据，让调用方继续 |
| L4 | 不可实现 | 返回 ERROR_CALL_NOT_IMPLEMENTED |

---

## 二、项目结构

```
cxx-runtime/
├── .github/workflows/build.yml          # CI 构建
├── Win10-Kernel32-Patcher/              # 旧版 Python 补丁（历史遗留，不再使用）
├── build_local.bat                      # 本地构建脚本
├── README.md                            # 中文用户文档
├── CHANGES_ConPTY.md                    # ConPTY 补丁变更说明
├── test_target.cpp                      # 测试目标程序（动态加载版）
├── test_target2.cpp                     # 测试目标程序（静态导入版）
│
└── Win10CompatRuntime/                  # ★ 核心项目
    ├── CMakeLists.txt                   # 根 CMake（C++17, MSVC /W3 /utf-8）
    ├── README.md                        # 英文技术文档
    │
    ├── CompatRuntime/                   # ★ 兼容运行时 DLL
    │   ├── CMakeLists.txt               # DLL 构建（GLOB 各子目录 *.cpp）
    │   ├── CompatRuntime.h              # 核心头文件（COMPAT_API 宏、工具函数）
    │   ├── CompatRuntime.cpp            # DllMain 入口（COMPAT_LOADER 触发 IAT 重写）
    │   ├── CompatRuntime.def            # 88 个导出符号定义
    │   ├── RuntimePatch.cpp             # 运行时 IAT 重定向（遍历 IAT + VirtualProtect 覆写）
    │   │
    │   ├── AdvApi/AdvApiProvider.cpp    # ETW 事件 API（3 个）
    │   ├── Console/ConsoleProvider.cpp  # ConPTY + 控制台扩展 API（9 个）
    │   ├── DPI/DPIProvider.cpp          # DPI 感知 API（13 个）
    │   ├── DLL/DLLProvider.cpp          # DLL 搜索路径 API（3 个）
    │   ├── File/FileProvider.cpp        # 文件 API（10 个）
    │   ├── Memory/MemoryProvider.cpp    # 内存 API（9 个）
    │   ├── Process/ProcessProvider.cpp  # 进程 API（17 个）
    │   ├── Sync/SyncProvider.cpp        # 同步 API（9 个）
    │   ├── Thread/ThreadProvider.cpp    # 线程 API（4 个）
    │   ├── Time/TimeProvider.cpp        # 时间 API（10 个）
    │   └── Utils/Utils.h               # 共享工具（OsProvides, HostBuild, HostAtLeast）
    │
    ├── CompatPatch/                     # ★ PE 静态补丁工具
    │   ├── CMakeLists.txt
    │   ├── main.cpp                     # CLI 入口（参数解析、数据库加载、调用 PatchImports）
    │   ├── PEUtils.h                    # PE 格式工具（RVA/文件偏移转换、节表操作，全部 inline）
    │   ├── PEUtils.cpp                  # 空实现（全部 inline 在头文件中）
    │   ├── ImportScanner.h/cpp          # 导入表扫描（标准导入 + 延迟导入）
    │   └── ImportRebuilder.h/cpp        # 导入表重建（添加 .compat 节，重定向 IAT）
    │
    ├── CompatLoader/                    # ★ 运行时注入器
    │   ├── CMakeLists.txt
    │   └── main.cpp                     # 挂起→注入→恢复
    │
    ├── Database/                        # ★ API 兼容性数据库
    │   ├── kernel32.csv                 # 44 条
    │   ├── kernelbase.csv               # 31 条
    │   ├── api-ms.csv                   # 39 条
    │   ├── user32.csv                   # 2 条
    │   └── advapi32.csv                 # 空（仅注释）
    │
    └── Tests/
        ├── CMakeLists.txt
        └── test_runtime.cpp             # 测试套件（Thread/Time/DPI/DLL/Sync）
```

---

## 三、核心代码详解

### 3.1 CompatRuntime.h — 统一头文件

**关键设计**：
- `COMPAT_API` 展开为 `extern "C"`，**不使用 `__declspec(dllexport)`**。导出由 `.def` 文件驱动，避免 C2375/C2491 链接冲突（SDK 头文件已声明这些 API 为 `dllimport`，重新声明为 `dllexport` 会冲突）。
- `#pragma warning(disable: 4273)` 抑制因重定义 SDK 声明产生的警告。
- `Compat_GetRealProc()` — 内联函数，通过 `GetModuleHandleA` + `GetProcAddress` 惰性解析真实 API。
- `Compat_GetWindowsBuild()` — 使用 `ntdll!RtlGetVersion` 绕过应用兼容性垫片获取真实 OS 版本。

### 3.2 CompatRuntime.def — 导出定义

共 **88 个导出符号**（87 个 Windows API 兼容实现 + 1 个内部工具 `Compat_ApplyToCurrentProcess`）。

**重要**：所有导出使用**原始 Windows API 名称**（如 `ClosePseudoConsole` 而非 `Compat_ClosePseudoConsole`），这样 PE loader 能直接从 CompatRuntime.dll 解析到正确的符号。

### 3.3 RuntimePatch.cpp — 运行时 IAT 重定向

当 CompatLoader 注入 DLL 后，DllMain 检测 `COMPAT_LOADER` 环境变量，调用 `Compat_RuntimePatchCurrentProcess()`：

1. `GetModuleHandleA(nullptr)` 获取宿主 EXE 基址
2. 遍历 `IMAGE_IMPORT_DESCRIPTOR` 数组
3. 对每个按名称导入的 API，检查是否在 Database 中
4. 若在 Database 中，用 `GetProcAddress(hCompat, apiName)` 获取 CompatRuntime 中的实现
5. `VirtualProtect(PAGE_READWRITE)` → 覆写 IAT 条目 → `VirtualProtect` 恢复保护

**注意**：仅处理 `IMAGE_THUNK_DATA64`（x64），不支持 x86。

### 3.4 ImportRebuilder.cpp — 静态 PE 补丁

核心算法（`PatchImports`）：

1. **扫描导入表**：调用 `ScanImports()` 找到所有需重定向的 API 及其 IAT/ILT 位置
2. **Decoy 替换**：将原始 ILT 条目改为同模块的第一个导入名（decoy），**不能置零**（零会截断 loader 的导入遍历，导致后续 API 未解析 → 崩溃）
3. **构建 .compat 节**：
   - 原始描述符数组（不变）+ N 个 CompatRuntime 描述符 + 哨兵
   - 每个 CompatRuntime 描述符的 `FirstThunk` 直接指向**原始 IAT 槽位**
   - 每个 CompatRuntime 描述符有独立的 2 项 ILT（1 个 IMAGE_IMPORT_BY_NAME + null）
   - 共享 "CompatRuntime.dll" DLL 名
4. **添加节头**：`.compat` 节，`IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ`
5. **更新 PE 头**：`NumberOfSections++`、`DataDirectory[IMPORT]` 指向新描述符数组、`SizeOfImage` 更新

**关键细节**：`resize()` 后必须重新推导所有 PE 指针（`nt`、`importDir`、`secTable`、`origDesc`），因为 vector 可能重新分配内存。

### 3.5 Provider 实现模式

所有 Provider 遵循统一模式：

```cpp
COMPAT_API 返回值 WINAPI ApiName(参数)
{
    // 1. 尝试转发到真实 OS API
    typedef 返回值 (WINAPI* PFN)(参数类型);
    static PFN pfn = nullptr;
    if (!pfn) pfn = (PFN)Compat_GetRealProc("kernel32", "ApiName");
    if (pfn) return pfn(实际参数);

    // 2. 降级到旧版 API 或模拟实现
    // ...
}
```

线程安全：
- `static PFN` 是线程安全的（C++11 magic statics 或指针赋值的原子性）
- 共享数据结构使用 `SRWLOCK`（如 `g_ptyMap`、`g_threadDesc`）

---

## 四、API 覆盖清单

### 4.1 按 Provider 分类

| Provider | 文件 | API 数量 | 兼容级别 | 关键实现 |
|----------|------|---------|---------|---------|
| **Thread** | ThreadProvider.cpp | 4 | L0/L2 | SetThreadDescription 用 `unordered_map<DWORD, wstring>` 存储；GetThreadDescription 返回 LocalAlloc 分配的缓冲区 |
| **Memory** | MemoryProvider.cpp | 9 | L1/L2/L3 | VirtualAlloc2 分派 NUMA→VirtualAllocExNuma；DiscardVirtualMemory/OfferVirtualMemory/ReclaimVirtualMemory 为 L3 桩 |
| **File** | FileProvider.cpp | 10 | L1/L2/L4 | CreateFile2→CreateFileW；GetFinalPathNameByHandleW 用 NtQueryObject 回退；OpenFileById 用 NtCreateFile(FILE_OPEN_BY_FILE_ID) 回退 |
| **Process** | ProcessProvider.cpp | 17 | L1/L2/L3/L4 | IsProcessInJob 用 NtQueryInformationProcess(ProcessIsInJob=7) 回退；CpuSetMasks 系列返回 0 masks |
| **Time** | TimeProvider.cpp | 10 | L1 | GetSystemTimePreciseAsFileTime 用 QPC 插值；GetTempPath2W→GetTempPathW |
| **Sync** | SyncProvider.cpp | 9 | L0/L2 | ConditionVariable 用 Event 实现（非 CriticalSection 包装）；WaitOnAddress 用 SRW+CV 近似 |
| **DPI** | DPIProvider.cpp | 13 | L1/L3 | GetDpiForWindow→GetDeviceCaps(LOGPIXELSX)；SetProcessDpiAwarenessContext 为 L3 桩 |
| **DLL** | DLLProvider.cpp | 3 | L0 | AddDllDirectory 返回固定 cookie；实际不修改搜索路径 |
| **AdvApi** | AdvApiProvider.cpp | 3 | L3 | ETW 事件 API 返回 ERROR_SUCCESS + 假句柄 |
| **Console** | ConsoleProvider.cpp | 9 | L0/L1 | ConPTY 三件套：L0 转发/L1 管道+cmd.exe 模拟；Console Ex 六件套：L0 转发/L1 旧版 API 填充 |

### 4.2 完整 API 列表（87 个导出）

```
AddDllDirectory                    AreDpiAwarenessContextsEqual
ClosePseudoConsole                 CopyFileExW
CreateFile2                        CreatePseudoConsole
DiscardVirtualMemory               EnableChildWindowDpiMessages
EnableNonClientDpiScaling          EventRegister
EventUnregister                    EventWrite
FindFirstStreamW                   FindNextStreamW
GetAwarenessFromDpiAwarenessContext
GetConsoleDisplayMode              GetConsoleScreenBufferInfoEx
GetCurrentApplicationUserModelId   GetCurrentConsoleFontEx
GetCurrentPackageId                GetDpiForMonitor
GetDpiForSystem                    GetDpiForWindow
GetFileInformationByHandleEx       GetFinalPathNameByHandleW
GetPackageFamilyName               GetProcessDpiAwareness
GetProcessDefaultCpuSetMasks       GetProcessDpiAwarenessContext
GetProcessInformation              GetProcessMitigationPolicy
GetStartupInfoW                    GetSystemTimeAsFileTime
GetSystemTimePrecise               GetSystemTimePreciseAsFileTime
GetTempPath2A                      GetTempPath2W
GetThreadDescription               GetThreadDpiAwarenessContext
GetThreadInformation               GetThreadSelectedCpuSets
GetTickCount64                     InitializeConditionVariable
InitOnceExecuteOnce                IsProcessCritical
IsProcessInJob                     MapViewOfFile3
MapViewOfFileFromApp               MoveFileExW
OfferVirtualMemory                 OpenFileById
PrefetchVirtualMemory              QueryFullProcessImageNameW
QueryInterruptTime                 QueryInterruptTimePrecise
QueryProcessAffinityUpdateMode     QueryUnbiasedInterruptTime
QueryUnbiasedInterruptTimePrecise  ReclaimVirtualMemory
ResizePseudoConsole                RemoveDllDirectory
SetConsoleDisplayMode              SetConsoleScreenBufferInfoEx
SetCurrentConsoleFontEx            SetDefaultDllDirectories
SetFileInformationByHandle         SetFileIoOverlappedRange
SetProcessAffinityUpdateMode       SetProcessDefaultCpuSetMasks
SetProcessDpiAwareness             SetProcessDpiAwarenessContext
SetProcessInformation              SetProcessMitigationPolicy
SetThreadDescription               SetThreadDpiAwarenessContext
SetThreadInformation               SetThreadSelectedCpuSets
SleepConditionVariableCS           SleepConditionVariableSRW
VirtualAlloc2                      VirtualAllocFromApp
VirtualProtectFromApp              WaitOnAddress
WakeAllConditionVariable           WakeByAddressAll
WakeByAddressSingle                WakeConditionVariable
```

### 4.3 Database CSV 格式

```csv
module,name,introduced_build,level,strategy,runtime_symbol
```

- `module`：原始 DLL 名（如 `kernel32`、`api-ms-win-core-console-l1-2-0`）
- `name`：API 名称
- `introduced_build`：引入该 API 的 Windows build 号（如 14393=1607, 16299=1709, 17763=1809, 18362=1903）
- `level`：兼容级别 L0-L4
- `strategy`：固定为 `Runtime`
- `runtime_symbol`：CompatRuntime 中的实现函数名（实际导出名与 `name` 相同，此字段为历史兼容保留）

---

## 五、构建与部署

### 5.1 构建

```bat
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
cmake -S Win10CompatRuntime -B build -G Ninja
cmake --build build
```

产物在 `build\bin\`：
- `CompatRuntime.dll`
- `CompatPatch.exe`
- `CompatLoader.exe`
- `CompatTests.exe`

### 5.2 部署

最小部署集：
```
目标目录/
├── target.exe.patched     # CompatPatch 输出
├── CompatRuntime.dll      # 兼容运行时
└── Database/              # 5 个 CSV 文件
    ├── kernel32.csv
    ├── kernelbase.csv
    ├── api-ms.csv
    ├── user32.csv
    └── advapi32.csv
```

### 5.3 使用

```bat
:: 方案 A：静态修补
CompatPatch.exe target.exe
:: 生成 target.exe.patched，与 CompatRuntime.dll 同目录运行

:: 方案 B：运行时注入
CompatLoader.exe target.exe
:: 不修改原文件，自动注入

:: 调试
set COMPAT_DIAG=1
CompatLoader.exe target.exe --debug
```

---

## 六、已知问题与待修复项

### 6.1 编译期潜在问题

| # | 问题 | 位置 | 严重性 | 说明 |
|---|------|------|--------|------|
| 1 | **GetSystemTimeAsFileTime 递归风险** | `TimeProvider.cpp:212` | 🔴 高 | 实现为 `::GetSystemTimeAsFileTime(lpSystemTimeAsFileTime)`。如果 IAT 被重定向（方案 B），`::` 仍可能调用到自身而非系统 API。需改为通过 `Compat_GetRealProc` 获取真实函数指针，或直接用 `GetSystemTime` + 转换 |
| 2 | **COPY_CALLBACK_CHUNK_FINISHED 常量** | `FileProvider.cpp:373` | 🟡 中 | `CopyFileExW` 的 progress callback 中使用了 `COPY_CALLBACK_CHUNK_FINISHED`，需确认该常量在目标 SDK 版本中存在。若不存在需定义为 `0x00000001` |
| 3 | **FILE_OPEN_BY_FILE_ID 常量** | `FileProvider.cpp:295` | 🟡 中 | `OpenFileById` 回退实现中使用了 `FILE_OPEN_BY_FILE_ID`，需确认 SDK 定义。若不存在需定义为 `0x00000008` |
| 4 | **NtCreateFile 函数签名** | `FileProvider.cpp:285-295` | 🟡 中 | `OpenFileById` 回退中使用了 `NtCreateFile`，但函数指针类型声明和 `OBJECT_ATTRIBUTES` 初始化代码被截断（文件读取限制），需确认完整性 |
| 5 | **psapi.lib 链接** | `FileProvider.cpp:7` | 🟢 低 | `#pragma comment(lib, "psapi.lib")` 硬编码链接，CMake 中未显式声明。MSVC 通常能处理，但其他工具链可能不行 |

### 6.2 运行时已知限制

| # | 限制 | 说明 |
|---|------|------|
| 1 | 仅支持 x64 | IAT 重定向使用 `IMAGE_THUNK_DATA64`，不支持 x86 |
| 2 | 不处理序号导入 | 按序号导入的 API 无法按名称匹配，跳过 |
| 3 | ConditionVariable 实现简化 | 使用 manual-reset Event，非真正的条件变量语义（可能存在虚假唤醒） |
| 4 | WaitOnAddress 近似实现 | 使用 SRW + CV 轮询，非真正的 futex 式等待（性能差，可能忙等待） |
| 5 | ConPTY L1 降级有限 | 管道+cmd.exe 不支持 ConPTY 渲染协议，仅提供基本交互通道 |
| 6 | DLL 搜索路径 API 为空操作 | AddDllDirectory/RemoveDllDirectory/SetDefaultDllDirectories 不实际修改搜索路径 |
| 7 | InitOnceExecuteOnce 非线程安全 | 回退实现使用简单状态检查 + Sleep 轮询，非原子操作 |

### 6.3 一致性待验证项

| # | 检查项 | 状态 |
|---|--------|------|
| 1 | `.def` 导出名与 CSV `name` 列完全一致 | ⚠️ 需逐一比对 |
| 2 | CSV `runtime_symbol` 列与实际函数名一致 | ⚠️ 需逐一比对 |
| 3 | 所有 Provider .cpp 被 CMakeLists.txt GLOB 覆盖 | ✅ 已确认 |
| 4 | 新增 API 在 kernel32.csv、kernelbase.csv、api-ms.csv 三处均有条目 | ⚠️ 部分仅在 kernel32.csv 有条目 |
| 5 | 编译通过无错误 | ❌ 未验证 |

---

## 七、最近变更历史

### 7.1 第一轮：ConPTY 补丁（解决原始报错）

**问题**：目标 exe 导入 `ClosePseudoConsole` 等 ConPTY API，Database 中无条目，patcher 跳过，旧系统 loader 报错。

**变更**：
1. 新增 `Console/ConsoleProvider.cpp`（ConPTY 三件套：Create/Resize/Close）
2. `CompatRuntime.def` 添加 3 个导出
3. `CMakeLists.txt` 添加 `CONSOLE_SRC`
4. 三个 CSV 文件各添加 3 条

### 7.2 第二轮：扩展 API 覆盖

**需求**：补充新版 Win10 有而旧版不具备的接口，尽量实现功能而非简单 return 0。

**新增 API（约 20 个）**：

| API | Provider | Build | Level | 回退策略 |
|-----|----------|-------|-------|---------|
| GetConsoleScreenBufferInfoEx | Console | 16299 | L1 | GetConsoleScreenBufferInfo + 默认色表 |
| SetConsoleScreenBufferInfoEx | Console | 16299 | L1 | SetConsoleScreenBufferSize + SetConsoleWindowInfo |
| GetCurrentConsoleFontEx | Console | 16299 | L1 | GetCurrentConsoleFont + 默认 Consolas |
| SetCurrentConsoleFontEx | Console | 16299 | L1 | 返回 TRUE（无可靠回退） |
| GetConsoleDisplayMode | Console | 16299 | L1 | 返回 0（非全屏） |
| SetConsoleDisplayMode | Console | 16299 | L1 | 返回当前缓冲区大小 |
| IsProcessInJob | Process | 14393 | L1 | NtQueryInformationProcess(ProcessIsInJob=7) |
| GetProcessDefaultCpuSetMasks | Process | 18362 | L1 | 返回 0 masks |
| SetProcessDefaultCpuSetMasks | Process | 18362 | L1 | 静默接受 |
| GetThreadSelectedCpuSets | Process | 18362 | L1 | 返回所有 CPU |
| SetThreadSelectedCpuSets | Process | 18362 | L1 | 静默接受 |
| GetStartupInfoW | Process | 14393 | L1 | GetStartupInfoA + 字段转换 |
| GetTempPath2W | Time | 17763 | L1 | GetTempPathW |
| GetTempPath2A | Time | 17763 | L1 | GetTempPathA |
| GetSystemTimeAsFileTime | Time | 14393 | L1 | 直接调用系统 API（⚠️ 递归风险） |
| OpenFileById | File | 14393 | L1 | NtCreateFile(FILE_OPEN_BY_FILE_ID) |
| SetFileIoOverlappedRange | File | 14393 | L1 | 返回 TRUE |
| CopyFileExW | File | 14393 | L1 | CopyFileW + 模拟 progress callback |
| MoveFileExW | File | 14393 | L1 | MoveFileW + MOVEFILE_REPLACE_EXISTING 处理 |

### 7.3 Bug 修复

| Bug | 修复 |
|-----|------|
| `Compat_EmitateCreatePty` 拼写错误 | 改为 `Compat_EmulateCreatePty` |
| `RtlNtStatusToDosError` 未声明 | 改为 GetProcAddress 动态解析 |
| `ULONG8` 非标准类型 | 改为 `ULONGLONG` |

---

## 八、待办事项与后续工作建议

### 8.1 紧急（编译验证）

- [ ] **执行编译**，修复所有编译错误
- [ ] **修复 GetSystemTimeAsFileTime 递归风险**：改为通过 `Compat_GetRealProc` 获取真实函数指针
- [ ] **验证 COPY_CALLBACK_CHUNK_FINISHED 和 FILE_OPEN_BY_FILE_ID 常量**存在性
- [ ] **确认 FileProvider.cpp 中 OpenFileById 的 NtCreateFile 回退代码完整性**（之前读取时被截断）

### 8.2 重要（功能完善）

- [ ] **补充 .def 与 CSV 一致性校验**：确保 87 个导出名与 CSV name 列完全匹配
- [ ] **为新增的 20 个 API 编写测试用例**（当前 test_runtime.cpp 仅覆盖 Thread/Time/DPI/DLL/Sync）
- [ ] **ConditionVariable 实现改进**：使用 auto-reset event + 计数器替代 manual-reset event，减少虚假唤醒
- [ ] **WaitOnAddress 实现改进**：考虑使用 NT Keyed Event（NtCreateKeyedEvent）替代 SRW+CV 轮询

### 8.3 增强（扩展覆盖）

- [ ] **补充更多 Win10 新增 API**：如 `GetProcessDefaultCpuPriorityClass`、`SetProcessDefaultCpuPriorityClass`、`GetMemoryErrorHandlingCapabilities`、`BadMemoryCallbackRoutine` 等
- [ ] **支持 x86 目标**：在 ImportRebuilder 和 RuntimePatch 中添加 `IMAGE_THUNK_DATA32` 处理
- [ ] **支持序号导入**：在 Database 中添加序号映射表
- [ ] **advapi32.csv 扩展**：当前为空，可添加 `EventSetInformation`、`EventAccessCheck` 等

### 8.4 优化

- [ ] **DLL 搜索路径 API 真实实现**：使用 `SetDllDirectory` + 进程内路径列表模拟
- [ ] **InitOnceExecuteOnce 线程安全**：使用 `InterlockedCompareExchange` 替代简单状态检查
- [ ] **ConPTY L1 改进**：考虑使用 `conhost.exe --headless`（若存在）替代 `cmd.exe`
- [ ] **构建系统**：将 `build_local.bat` 中的硬编码路径改为相对路径

---

## 九、关键设计决策记录

| 决策 | 原因 | 替代方案 |
|------|------|---------|
| 使用 `.def` 文件导出而非 `__declspec(dllexport)` | 避免 C2375/C2491 链接冲突（SDK 已声明这些 API 为 dllimport） | 使用 `#pragma warning(disable)` + dllexport，但不够可靠 |
| 导出名使用原始 Windows API 名 | PE loader 按名称解析，必须与导入名完全匹配 | 使用前缀名（如 `Compat_ClosePseudoConsole`）+ 转发表，但增加复杂度 |
| 静态修补使用 decoy 而非置零 ILT | 零 ILT 条目会截断 loader 导入遍历，导致后续 API 未解析崩溃 | 置零 + 重排描述符，但更复杂且可能破坏对齐 |
| ConditionVariable 用 Event 实现 | 旧系统无 SRW + CV 原生支持，Event 是最通用的同步原语 | 使用 Semaphore，但语义更不匹配 |
| ConPTY L1 用 cmd.exe 模拟 | cmd.exe 在所有 Windows 版本上都存在，提供基本交互通道 | 使用 conhost.exe，但旧版本可能不支持所需参数 |
| `Compat_GetWindowsBuild` 用 RtlGetVersion | 绕过应用兼容性垫片（VerifyVersionInfo/GetVersionEx 可能返回假版本） | 读取注册表 `HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\CurrentBuild`，但需额外权限 |

---

## 十、依赖与构建环境

| 依赖 | 版本 | 用途 |
|------|------|------|
| Visual Studio 2022 Build Tools | 17.x | 编译器 (MSVC 19.x) |
| Windows SDK | 10.0.26100 | 头文件 + 导入库 |
| CMake | 3.20+ | 构建系统 |
| Ninja | 最新 | 构建后端（可选，可用 VS 生成器） |

**运行时依赖**：
- CompatRuntime.dll 链接：`kernel32.lib`、`user32.lib`、`psapi.lib`、`shcore.lib`（DPI API）
- CompatPatch.exe：纯 C++，仅依赖 kernel32
- CompatLoader.exe：仅依赖 kernel32

---

## 十一、文件校验清单

以下为项目所有源文件的行数统计，供交接时校验完整性：

| 文件 | 行数 | 说明 |
|------|------|------|
| CompatRuntime.h | 91 | 核心头文件 |
| CompatRuntime.cpp | 56 | DllMain |
| CompatRuntime.def | 98 | 导出定义 |
| RuntimePatch.cpp | 180 | 运行时 IAT 重写 |
| Utils.h | 52 | 工具函数 |
| AdvApiProvider.cpp | 61 | ETW API |
| ConsoleProvider.cpp | 460 | ConPTY + Console Ex |
| DPIProvider.cpp | 202 | DPI API |
| DLLProvider.cpp | 42 | DLL 搜索路径 |
| FileProvider.cpp | 397 | 文件 API |
| MemoryProvider.cpp | 175 | 内存 API |
| ProcessProvider.cpp | 332 | 进程 API |
| SyncProvider.cpp | 164 | 同步 API |
| ThreadProvider.cpp | 142 | 线程 API |
| TimeProvider.cpp | 212 | 时间 API |
| PEUtils.h | 127 | PE 工具 |
| ImportScanner.h | 25 | 扫描接口 |
| ImportScanner.cpp | 103 | 扫描实现 |
| ImportRebuilder.h | 34 | 重建接口 |
| ImportRebuilder.cpp | 298 | 重建实现 |
| CompatPatch/main.cpp | 278 | 补丁工具 CLI |
| CompatLoader/main.cpp | 171 | 注入器 |
| test_runtime.cpp | 162 | 测试套件 |
| kernel32.csv | 44 条 | kernel32 数据库 |
| kernelbase.csv | 31 条 | kernelbase 数据库 |
| api-ms.csv | 39 条 | API 集数据库 |
| user32.csv | 2 条 | user32 数据库 |
| advapi32.csv | 0 条 | advapi32 数据库（空） |

---

*文档结束。接手者请优先执行第八章"紧急"项，确保项目可编译运行。*
