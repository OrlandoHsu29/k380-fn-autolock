# K380 Fn Auto Lock for Windows

<p align="center"><img src="media/k380-fn-autolock-logo.png" alt="K380 Fn Auto Lock 图标" width="180"></p>

让罗技 K380 在 Windows 上默认使用 F1–F12，并在登录、键盘重新连接或电脑唤醒后自动恢复这个设置。只匹配 K380 型号，无需填写键盘的蓝牙地址。

## 主要解决的问题

| 使用场景 | 本项目的处理方式 |
| --- | --- |
| 手动切换按键模式 | 提供 `setFnKeys.exe` 和 `setMediaKeys.exe`；设备未就绪或写入失败时返回错误 |
| 电脑重启 | 当前用户登录时启动后台程序，自动重新设置 Fn 模式 |
| 键盘重启或蓝牙断开后重连 | 监听设备变化并重试，设备就绪后自动恢复 |
| 切到其他蓝牙通道，再切回电脑 | 监听蓝牙连接；连接后每 2 秒尝试一次，持续 12 秒，通常可在数秒内恢复 |
| Windows 未发出可用的连接通知 | 后台每 60 秒补写一次作为兜底 |
| x86 / x64 编译 | 构建脚本按 GCC 目标架构选择仓库内对应架构的 HIDAPI 库 |

为实现自动恢复，`k380FnAutoLock.exe` 需要在后台持续运行。它使用 Windows 自带的系统托盘和原生控件，不依赖额外 GUI 框架。右键托盘图标可打开菜单；左键图标或选择“打开设置”会显示一个小型设置窗口。设置窗口右上角的 X 只关闭窗口；窗口底部的红色“退出程序”按钮和托盘菜单中的同名选项会结束当前进程，但保留登录自启设置。程序使用随项目提供的多尺寸 ICO 作为 exe 文件、托盘和设置窗口图标，无需额外运行依赖；若只想手动设置一次，可以仅使用两个手动命令。

## 界面预览

![K380 Fn Auto Lock 设置窗口](media/run.png)

## 工作方式

程序通过 Logitech VID `046D`、K380 PID `B342`，以及 HID Usage Page `FF00` / Usage `0001` 找到目标接口；不会按某一把键盘的蓝牙 MAC 地址匹配。后台程序在启动时设置 Fn 模式，并监听 HID 设备变化、蓝牙连接和系统唤醒事件。

切回 Windows 蓝牙通道后，键盘可能先报告连接、再开放可写的 HID 接口，因此蓝牙连接事件后会在 12 秒内持续重试。键盘未连接时，每 2 秒重试；正常运行时，每 60 秒补写一次，以处理连接通知缺失的情况。恢复时间取决于 Windows 和键盘何时提供 HID 接口，不能保证每次都在固定秒数内完成。

菜单和设置窗口提供登录后自启、Fn 锁定、解除 Fn 锁定和显示/隐藏托盘图标选项。当前生效的 Fn 模式会显示勾选并禁用，另一项可选。模式选择会保存到当前用户设置，并在键盘重连后继续应用；如果键盘尚未连接，窗口会显示等待状态。设置界面显示的是程序保存并尝试应用的模式，键盘本身不提供可供程序读取的当前 Fn 状态。

如果不想长期看到托盘图标，可在菜单或设置窗口中隐藏。后台仍会继续运行和恢复键盘设置；再次打开 `k380FnAutoLock.exe` 会唤起原进程并显示设置窗口，不会启动第二个后台进程。自动启动使用当前用户的 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` 项，在**用户登录后**生效，不需要管理员权限。程序不联网，不上传设备信息，也不要求输入蓝牙地址；安装脚本只在本机复制程序文件并写入上述启动项。

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
