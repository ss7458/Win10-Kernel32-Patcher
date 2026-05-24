import sys
import os
import shutil

# 补丁定义
PATCHES = {
    "set": (
        b"SetThreadDescription",
        b"SetThreadPriority\x00\x00\x00",
        "SetThreadDescription → SetThreadPriority",
    ),
    "get": (
        b"GetThreadDescription",
        b"GetThreadPriority\x00\x00\x00",
        "GetThreadDescription → GetThreadPriority",
    ),
    "precise_time": (
        b"GetSystemTimePreciseAsFileTime",
        b"GetSystemTimeAsFileTime\x00\x00",
        "GetSystemTimePreciseAsFileTime → GetSystemTimeAsFileTime",
    ),
    "init_cond": (
        b"InitializeConditionVariable",
        b"InitializeCriticalSection\x00",
        "InitializeConditionVariable → InitializeCriticalSection",
    ),
    "query_image": (
        b"QueryFullProcessImageNameW",
        b"GetModuleFileNameW\x00\x00\x00\x00",
        "QueryFullProcessImageNameW → GetModuleFileNameW",
    ),
}


def choose_patch_mode():
    print("=" * 70)
    print(" Win10 旧版 KERNEL32 入口点修复工具")
    print("=" * 70)
    print("Please select which function to fix:")
    print("1. Fix SetThreadDescription")
    print("2. Fix GetThreadDescription")
    print("3. Fix GetSystemTimePreciseAsFileTime (Experimental)")
    print("4. Fix InitializeConditionVariable (Experimental)")
    print("5. Fix QueryFullProcessImageNameW (Experimental)")
    print("6. Fix All (Recommended)")
    print("0. Cancel")
    print("-" * 70)

    while True:
        choice = input("请输入选项 (1/2/3/4/5/6/0): ").strip()
        if choice in ["1", "2", "3", "4", "5", "6"]:
            return choice
        if choice == "0":
            print("已取消操作。")
            return None
        print("输入错误，请重新输入！")


def patch_file(file_path):
    mode = choose_patch_mode()
    if mode is None:
        return

    if not os.path.isfile(file_path):
        print(f"[错误] 找不到文件: {file_path}")
        return

    backup_path = file_path + ".bak"
    if os.path.isfile(backup_path):
        print(f"[提示] 备份文件已存在: {backup_path}")
    else:
        try:
            shutil.copy2(file_path, backup_path)
            print(f"[成功] 已创建备份: {backup_path}")
        except Exception as e:
            print(f"[错误] 创建备份失败: {e}")
            return

    try:
        with open(file_path, "rb") as f:
            content = f.read()

        applied = []
        new_content = content

        to_patch = []
        if mode == "1":
            to_patch = ["set"]
        elif mode == "2":
            to_patch = ["get"]
        elif mode == "3":
            to_patch = ["precise_time"]
        elif mode == "4":
            to_patch = ["init_cond"]
        elif mode == "5":
            to_patch = ["query_image"]
        elif mode == "6":
            to_patch = ["set", "get", "precise_time", "init_cond", "query_image"]

        for key in to_patch:
            target, replacement, label = PATCHES[key]
            count = new_content.count(target)
            if count > 0:
                new_content = new_content.replace(target, replacement)
                applied.append(f"  - {label}（{count} 处）")
            else:
                print(f"[提示] 未找到: {target.decode('ascii', errors='ignore')}")

        if not applied:
            print("[提示] 未找到任何可替换的字符串。")
            print("文件可能已被修改过，或不包含这些 API。")
            return

        with open(file_path, "wb") as f:
            f.write(new_content)

        print("\n" + "=" * 70)
        print("[成功] 补丁应用完成！")
        for line in applied:
            print(line)
        print("=" * 70)
        print("请重新启动程序进行测试。")
        print("=" * 70)

    except PermissionError:
        print("[错误] 权限被拒绝！请关闭占用该文件的程序后重试。")
    except Exception as e:
        print(f"[错误] 处理文件时发生异常: {e}")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("【使用方法】")
        print("请将需要修复的文件（.exe 或 .dll）拖放到此程序上。")
    else:
        patch_file(sys.argv[1])

    print("\n")
    input("按回车键退出程序...")
