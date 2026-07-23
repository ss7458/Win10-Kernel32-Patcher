# ConPTY 兼容补丁变更说明

## 背景

目标 exe 导入了 `ClosePseudoConsole` 等 ConPTY API（Windows 10 1809+ 引入），但项目 Database 中缺少这些 API 条目，导致 `CompatPatch.exe` 跳过它们，原始导入仍指向 `kernel32.dll`，旧系统上 loader 报错"无法定位程序输入点 ClosePseudoConsole 于动态链接库"。

本次变更在 CompatRuntime 中新增了三个 ConPTY API 的兼容 shim，并更新 Database 使 patcher 能识别并重定向它们。

---

## 变更文件清单

| # | 文件 | 操作 | 说明 |
|---|------|------|------|
| 1 | `Win10CompatRuntime/CompatRuntime/Console/ConsoleProvider.cpp` | **新增** | 三个 ConPTY API 的兼容实现 |
| 2 | `Win10CompatRuntime/CompatRuntime/CompatRuntime.def` | 修改 | 添加三个导出名 |
| 3 | `Win10CompatRuntime/CompatRuntime/CMakeLists.txt` | 修改 | 注册 Console 源文件目录 |
| 4 | `Win10CompatRuntime/Database/kernel32.csv` | 修改 | 添加三个 API 条目 |
| 5 | `Win10CompatRuntime/Database/kernelbase.csv` | 修改 | 添加三个 API 条目 |
| 6 | `Win10CompatRuntime/Database/api-ms.csv` | 修改 | 添加三个 API 条目 |

---

## 各文件详细变更

### 1. `Console/ConsoleProvider.cpp`（新增，308 行）

实现 `CreatePseudoConsole`、`ResizePseudoConsole`、`ClosePseudoConsole` 三个 API，采用 L0/L1 双层策略：

| API | L0（真实 API 存在时） | L1（旧系统降级） |
|-----|----------------------|-----------------|
| `CreatePseudoConsole` | 转发到 `kernel32.dll`/`kernelbase.dll` 真实实现 | 创建匿名管道 + 启动隐藏 `cmd.exe` 子进程，用 `CompatPtyContext` 跟踪句柄，返回 HPCON |
| `ResizePseudoConsole` | 转发到真实 API | 记录请求尺寸到 context，返回 `S_OK` |
| `ClosePseudoConsole` | 转发到真实 API | 发送 `exit` 命令、等待退出、终止进程、关闭句柄、释放 context |

关键设计：
- `Compat_IsEmulatedHPCON()` 通过查映射表区分自建 HPCON 与 OS 真实 HPCON
- `Compat_ResolveRealConPty()` 惰性解析真实 API，依次尝试 `kernel32.dll` 和 `kernelbase.dll`
- `SRWLOCK` 保护 `g_ptyMap`，线程安全

### 2. `CompatRuntime.def`

按字母序插入三个导出：

```
ClosePseudoConsole      （第 12 行，AreDpiAwarenessContextsEqual 之后）
CreatePseudoConsole     （第 14 行，CreateFile2 之后）
ResizePseudoConsole     （第 56 行，ReclaimVirtualMemory 之后）
```

### 3. `CMakeLists.txt`

```cmake
file(GLOB CONSOLE_SRC Console/*.cpp)          # 第 13 行，新增
```

```cmake
${TIME_SRC} ${SYNC_SRC} ${DPI_SRC} ${DLL_SRC} ${CONSOLE_SRC} ${UTIL_SRC}  # 第 21 行，加入 ${CONSOLE_SRC}
```

### 4-6. Database CSV 文件

三个 CSV 文件各添加三行，格式：`module,name,introduced,level,strategy,runtime`

**kernel32.csv**（第 25-27 行）：
```csv
kernel32,CreatePseudoConsole,17763,L1,Runtime,Compat_CreatePseudoConsole
kernel32,ResizePseudoConsole,17763,L1,Runtime,Compat_ResizePseudoConsole
kernel32,ClosePseudoConsole,17763,L1,Runtime,Compat_ClosePseudoConsole
```

**kernelbase.csv**（第 12-14 行）：
```csv
kernelbase,CreatePseudoConsole,17763,L1,Runtime,Compat_CreatePseudoConsole
kernelbase,ResizePseudoConsole,17763,L1,Runtime,Compat_ResizePseudoConsole
kernelbase,ClosePseudoConsole,17763,L1,Runtime,Compat_ClosePseudoConsole
```

**api-ms.csv**（第 20-22 行）：
```csv
api-ms-win-core-console-l1-2-0,CreatePseudoConsole,17763,L1,Runtime,Compat_CreatePseudoConsole
api-ms-win-core-console-l1-2-0,ResizePseudoConsole,17763,L1,Runtime,Compat_ResizePseudoConsole
api-ms-win-core-console-l1-2-0,ClosePseudoConsole,17763,L1,Runtime,Compat_ClosePseudoConsole
```

---

## 运行时行为

```
patch 后的 exe 启动
  │
  ├─ loader 从 CompatRuntime.dll 解析 ConPTY API（不再报错）
  │
  ├─ Win10 1809+（build ≥ 17763）
  │    └─ Compat_ResolveRealConPty() 成功 → 直接转发到真实 ConPTY（L0，功能完整）
  │
  └─ 旧系统（build < 17763）
       └─ 降级到管道 + cmd.exe 子进程模拟（L1）
            ├─ CreatePseudoConsole: 创建管道、启动隐藏 cmd.exe、返回 HPCON
            ├─ ResizePseudoConsole: 记录尺寸、返回 S_OK
            └─ ClosePseudoConsole: 发送 exit、等待退出、清理资源
```

---

## 验证方式

重新编译后运行：

```
CompatPatch.exe target.exe
```

输出应包含：

```
  kernel32.dll!ClosePseudoConsole  -> CompatRuntime.dll!ClosePseudoConsole
  kernel32.dll!CreatePseudoConsole -> CompatRuntime.dll!CreatePseudoConsole
  kernel32.dll!ResizePseudoConsole -> CompatRuntime.dll!ResizePseudoConsole
```

将生成的 `target.exe.patched` 与 `CompatRuntime.dll` 放在同一目录运行即可。
