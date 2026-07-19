# Win10 兼容运行时（Win10 Compatibility Runtime）

> **一句话**：让“依赖新版 Windows 接口”的程序，能在你的电脑上正常跑起来。
> **原理**：把程序对系统 `kernel32` / `api-ms` 的某些函数调用，悄悄转到我们提供的兼容层 `CompatRuntime.dll`。

---

## 这东西解决什么问题？

有些软件是用**较新版本 Windows** 的接口编译的（例如 `GetThreadDescription`、`SetThreadDescription`、`WaitOnAddress`、`GetDpiForMonitor` 等）。当你在另一台 Windows 上运行它们时，如果系统里没有这些接口，程序可能启动报错或直接闪退。

本工具提供两套方案，把程序“缺”的接口补上：

| 方案 | 工具 | 特点 |
|------|------|------|
| **A. 静态修补** | `CompatPatch.exe` | 直接改 exe 文件，生成 `.patched`，以后每次跑这个文件 |
| **B. 运行时注入** | `CompatLoader.exe` | 不改文件，临时注入，跑一次原 exe |

---

## 怎么用（小白版）

### 第 1 步：准备文件夹（只需一次）
新建一个文件夹，把下面 **4 样东西**放进去：

- `CompatPatch.exe`（或 `CompatLoader.exe`）
- `CompatRuntime.dll`
- `Database\` 文件夹（里面是 5 个 `.csv` 规则表，告诉工具要重定向哪些函数）
- （可选）本说明文件

> **小知识**：`Database` 是“规则书”，**全局共用，不用为每个程序单独准备**。
> 工具默认会在自己旁边找 `Database` 文件夹，找不到时才需要加 `--database` 参数。
> 所以最省事的做法就是：把 `Database` 文件夹和 exe 放一起，然后零参数运行。

### 第 2 步：选一种方案

#### 方案 A：静态修补（推荐，一劳永逸）
1. 把你要运行的程序（比如 `老软件.exe`）也放进这个文件夹。
2. 在文件夹空白处，地址栏输入 `cmd` 回车，打开命令行，运行：
   ```
   CompatPatch.exe 老软件.exe
   ```
3. 会生成 `老软件.exe.patched`。**把 `CompatRuntime.dll` 放在 `.patched` 同一个文件夹**，然后双击 `老软件.exe.patched` 运行即可。

#### 方案 B：运行时注入（不想改文件时用）
1. 同样把程序和 `CompatRuntime.dll` 放好。
2. 命令行运行：
   ```
   CompatLoader.exe 老软件.exe
   ```
   工具会自动：挂起启动程序 → 注入 `CompatRuntime.dll` → 改写接口指向 → 恢复运行。原 `老软件.exe` 一个字节都不用改。

---

## 怎么确认生效了？

- **方案 A**：看 `CompatPatch.exe` 的输出里有没有 `Redirected N API(s)`。
- **方案 B**：先设置环境变量 `COMPAT_DIAG=1` 再运行，会打印重定向了几个接口，例如：
  ```
  [DIAG] redirected host import: GetThreadDescription
  [DIAG] CompatRuntime redirected 1 IAT import(s) in host EXE.
  ```

---

## 注意事项

- **仅支持 64 位（x64）程序**。32 位程序暂不支持。
- 只重定向“按函数名”导入的接口；按序号导入的不处理。
- 静态方式（方案 A）下，`CompatRuntime.dll` 必须和 `.patched` 放在**同一文件夹**；注入方式（方案 B）由 Loader 自动处理，无需手动放。

---

## 从源码构建（进阶）

需要：Visual Studio 2022 生成工具 + Windows SDK 10.0.26100 + CMake + Ninja。

```bat
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
cmake -S Win10CompatRuntime -B build -G Ninja
cmake --build build
```

构建产物在 `build\bin\`（含 `CompatPatch.exe`、`CompatLoader.exe`、`CompatRuntime.dll`）。
规则表在 `Win10CompatRuntime\Database\`。
