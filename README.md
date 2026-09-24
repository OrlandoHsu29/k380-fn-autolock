# K380 Fn Auto Lock for Windows

让罗技 K380 在 Windows 上默认使用 F1–F12，并在登录、键盘重新连接或电脑唤醒后自动恢复这个设置。只匹配 K380 型号，无需填写键盘的蓝牙地址。

## 相比原项目解决了什么

| 场景 | 原项目 | 当前版本 |
| --- | --- | --- |
| 手动切换按键模式 | 运行一次 `setFnKeys.exe` 或 `setMediaKeys.exe` | 保留这两个命令，并在设备未就绪或写入失败时返回错误 |
| 电脑重启 | Fn 设置失效后需要再次手动运行 | 当前用户登录时启动后台程序，自动重新设置 |
| 键盘重启或蓝牙断开后重连 | 需要再次手动运行 | 监听设备变化并重试，设备就绪后自动恢复 |
| 切到其他蓝牙通道，再切回电脑 | 原项目没有后台监听 | 监听蓝牙连接；连接后每 2 秒尝试一次，持续 12 秒，通常可在数秒内恢复 |
| Windows 未发出可用的连接通知 | 需要再次手动运行 | 后台每 60 秒补写一次作为兜底 |
| 编译架构 | 原说明只给出 x86 编译命令 | 构建脚本按 GCC 目标架构选择随仓库提供的 x86 或 x64 HIDAPI 库 |

为实现自动恢复，`k380FnAutoLock.exe` 需要在后台持续运行。它没有可见窗口；若只想手动设置一次，可以仅使用两个手动命令。

## 工作方式

程序通过 Logitech VID `046D`、K380 PID `B342`，以及 HID Usage Page `FF00` / Usage `0001` 找到目标接口；不会按某一把键盘的蓝牙 MAC 地址匹配。后台程序在启动时设置 Fn 模式，并监听 HID 设备变化、蓝牙连接和系统唤醒事件。

切回 Windows 蓝牙通道后，键盘可能先报告连接、再开放可写的 HID 接口，因此蓝牙连接事件后会在 12 秒内持续重试。键盘未连接时，每 2 秒重试；正常运行时，每 60 秒补写一次，以处理连接通知缺失的情况。恢复时间取决于 Windows 和键盘何时提供 HID 接口，不能保证每次都在固定秒数内完成。

自动启动使用当前用户的 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` 项，在**用户登录后**生效，不需要管理员权限。程序不联网，不上传设备信息，也不要求输入蓝牙地址；安装脚本只在本机复制程序文件并写入上述启动项。

## 编译和安装

需要 Windows、面向 Windows 的 MinGW GCC，以及仓库内的 HIDAPI 库。`arm-none-eabi-gcc` 面向嵌入式目标，不能用于编译此 Windows 程序。在项目根目录的 PowerShell 中运行：

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
powershell -ExecutionPolicy Bypass -File .\startup.ps1
```

`build.ps1` 默认从 `PATH` 查找 `gcc.exe`。如果 GCC 不在 `PATH` 中，可用 `-Compiler` 传入本机 `gcc.exe` 的完整路径：

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1 -Compiler '<MinGW 安装目录>\bin\gcc.exe'
```

构建产物位于 `build/`：

| 文件 | 作用 |
| --- | --- |
| `k380FnAutoLock.exe` | 自动恢复 Fn 模式的后台程序 |
| `setFnKeys.exe` | 手动设置 F1–F12 优先 |
| `setMediaKeys.exe` | 手动设置媒体键优先 |
| `hidapi.dll` | 与 GCC 目标架构对应的 HIDAPI 动态库 |
| `LICENSE-hidapi-bsd.txt` | 随 HIDAPI 动态库附带的授权文本 |

`startup.ps1` 会把后台程序、DLL 和 HIDAPI 授权文本复制到 `installed/`，设置当前用户登录时自动启动，并立即运行。更新程序后，再次运行该脚本即可启用新版本；使用期间请保留 `installed/` 目录。要取消自动启动并关闭后台程序：

```powershell
powershell -ExecutionPolicy Bypass -File .\startup.ps1 -Remove
```

如果想长期使用媒体键优先，先执行上面的移除命令，再运行 `.\build\setMediaKeys.exe`。后台程序运行时会再次恢复 Fn 模式。若自动恢复没有生效，可运行 `.\build\setFnKeys.exe`，根据“未找到 K380”或写入错误信息判断设备是否已准备好。

## VS Code 开发

安装 C/C++ 扩展后，可在本机配置 MinGW GCC 和 GDB。项目的 `.vscode/` 包含本机专用路径，已加入 `.gitignore`，不会随源码提交。调试后台程序前，先运行 `startup.ps1 -Remove` 关闭已运行的实例，否则单实例保护会让新的调试进程退出。

## 来源与授权

本项目基于 [dheygere/k380-fn-lock-for-windows](https://github.com/dheygere/k380-fn-lock-for-windows)。原项目提到的设备命令来源是 [k810fn](https://github.com/keighrim/k810fn/blob/master/win/k810fn/k810fnCLI.cpp) 和 [k380-function-keys-conf](https://github.com/jergusg/k380-function-keys-conf/blob/master/k380_conf.c)。所用 HIDAPI 来自 [libusb/hidapi](https://github.com/libusb/hidapi)，按其 [BSD 条款](hidapi/LICENSE-bsd.txt)使用和再分发。

原项目仓库目前没有附带授权许可证。本仓库因此没有给原项目代码擅自指定许可证；公开再分发或为整个项目选择开源许可证前，需要先确认原作者授予的使用与再分发权限。
