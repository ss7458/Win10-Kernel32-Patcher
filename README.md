# Win10-Kernel32-Patcher

**Windows 10 旧版 KERNEL32 入口点修复工具**

一个轻量级、实用的兼容性修复工具，用于解决新版软件在 Windows 10 系统上因缺失较新 KERNEL32.dll API 而导致的崩溃问题。

常见错误提示：
> 无法定位程序输入点 GetThreadDescription 于动态链接库 KERNEL32.dll


---

## ✨ 功能特性

- 支持修复多个常见的 Win10 不兼容 API
- 一键拖拽使用，无需安装
- 自动创建原文件备份（`.bak`）
- 中文界面，操作简单

### 支持修复的 API

| 选项 | API 函数                        | 替换为                          | 常见场景                  |
|------|--------------------------------|---------------------------------|---------------------------|
| 1    | SetThreadDescription           | SetThreadPriority               | Unity 游戏、多数现代程序  |
| 2    | GetThreadDescription           | GetThreadPriority               | Bun、Node.js 相关程序     |
| 3    | GetSystemTimePreciseAsFileTime | GetSystemTimeAsFileTime         | 高精度时间相关软件        |
| 4    | InitializeConditionVariable    | InitializeCriticalSection       | 多线程同步相关程序        |
| 5    | QueryFullProcessImageNameW     | GetModuleFileNameW              | 进程路径获取相关软件      |

---

## 📌 作者实测修复记录


| 软件名称     | 版本       | 是否修复成功 | 备注 |
|--------------|------------|--------------|------|
| OpenCode    | 1.15.10     | ✅ 已修复     | 启动后需等待约 5 分钟 |
| Kilo.exe    | 7.xx.xx         | ✅ 已修复     | 完全正常运行 |

> **OpenCode 已知问题**：首次启动时仍然需要等待约 **5 分钟** 才能正常进入界面通过日志.local\share\opencode\log分析，是因为程序会读取 `.cache\opencode\models.json` 直到超时结束"Timed out waiting for lock: models-dev:...\models.json"。

---

## 🚀 使用方法

1. 下载本工具（`Patcher.py`）
2. 将需要修复的文件（`.exe` 或 `.dll`）**直接拖拽** 到本程序上
3. 选择要修复的选项（推荐只选择 **GetThreadDescription**）
4. 等待提示「补丁应用完成」
5. 重新启动你的程序进行测试

**支持文件**：`UnityPlayer.dll`、`opencode.exe`、`kilo.exe` 等

---

## ⚠️ 注意事项

- 使用前请确保目标程序已关闭
- 程序会自动备份原始文件为 `.bak` 后缀
- 如需还原，直接将 `.bak` 文件改回原文件名即可
- 本工具仅修改字符串替换，不会对系统文件进行操作

---

## 📄 License

MIT License

---

## 思路来源

本项目思路来源于 [CNOS0594/Fix-Unity-Win10](https://github.com/CNOS0594/Fix-Unity-Win10)，在此表示感谢。
