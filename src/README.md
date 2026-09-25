# 工作流程与实现说明

本文记录 K380 Fn Auto Lock 的运行流程、设置保存方式和构建方法。面向希望了解或修改源码的开发者。

## 代码分层

| 文件 | 职责 |
| --- | --- |
| `src/app/main.c` | 程序入口、设备与唤醒事件监听、恢复调度 |
| `src/device/k380_hid.c` / `k380_hid.h` | 查找 K380 HID 接口并发送按键模式报告 |
| `src/app/app_settings.c` / `app_settings.h` | 保存 Fn 模式、图标显示和开机自启设置 |
| `src/app/app_ui.c` / `app_ui.h` | 设置窗口、托盘图标和菜单 |
| `src/app/app_state.c` / `app_state.h` | 后台运行期间共享的应用状态 |
| `resources/app-icon.rc` | 将 `media/` 下的 ICO 图标嵌入程序 |
| `build.ps1` | 按目标架构编译各模块并链接为程序 |

## 设备与按键模式

程序通过以下 HID 信息查找目标键盘：

- Logitech VID：`046D`
- K380 PID：`B342`
- Usage Page：`FF00`
- Usage：`0001`

程序按型号匹配，不使用蓝牙 MAC 地址。找到匹配接口后，打开 HID 路径并发送对应的七字节报告：Fn 锁定模式让 F1–F12 优先；媒体键模式让音量、播放等媒体功能优先。

键盘没有提供可供此程序读取的 Fn 模式状态，因此设置窗口展示的是程序保存并尝试应用的模式，不是从键盘读取的实际状态。

## 后台恢复流程

启动 `k380FnAutoLock.exe` 后，程序按以下顺序工作：

1. 使用单实例互斥量防止启动重复后台进程。再次运行 exe 会唤起现有进程的设置窗口。
2. 从当前用户注册表读取 Fn 模式和托盘图标显示设置；未保存过时默认选择 Fn 锁定并显示托盘图标。
3. 初始化 HIDAPI，创建用于接收 Windows 通知的隐藏窗口，并注册 HID 设备通知和蓝牙适配器连接事件。
4. 启动托盘图标，立即尝试向 K380 写入已保存的按键模式。
5. 处理连接和唤醒事件，并按当前状态安排下一次检查。

收到设备到达、移除、设备树变化、蓝牙连接或电脑唤醒事件后，程序会在约两秒后尝试重新应用设置。如果尚未找到 K380，会在最多 12 秒的窗口内每两秒重试，以等待 HID 接口出现。如果已经找到接口但打开或写入失败，会停止快速重试，避免持续访问可能正在休眠的设备。

没有设备事件时，程序每 15 分钟检查一次。只有最近 30 秒内有用户输入时才定时补写 Fn 设置；电脑空闲时跳过定时补写，设备事件仍会触发恢复。失败后的普通重试也采用 15 分钟间隔，并在电脑空闲时暂停。如果 Windows 漏掉连接事件，再次运行 exe 也会主动安排一次恢复。具体恢复时间取决于 Windows 和键盘何时准备好 HID 接口。

## 设置与托盘行为

- Fn 模式和托盘图标显示状态保存在 `HKCU\Software\K380FnAutoLock`。
- 开机自启使用当前用户的 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`，不需要管理员权限。
- 托盘菜单和设置窗口可切换 Fn 锁定、媒体键优先、开机自启和托盘图标显示状态。
- 隐藏托盘图标不会关闭后台程序。再次运行 exe 会打开现有进程的设置窗口。
- 首次手动运行 exe 会直接显示设置窗口；开机自启命令使用后台参数，不弹出窗口。


## 构建

在 Windows 上使用面向 Windows 的 MinGW GCC，在项目根目录运行：

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

如果 GCC 不在 `PATH` 中，可指定 `gcc.exe`：

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1 -Compiler '<MinGW 安装目录>\bin\gcc.exe'
```

打包时可加 `-Release`，使用体积较小的优化构建；省略时保留调试信息，方便源码调试：

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1 -Compiler '<MinGW 安装目录>\bin\gcc.exe' -Release
```

构建脚本根据 GCC 目标架构选择 `hidapi/x86` 或 `hidapi/x64` 库，并用 `windres` 编译 `resources/app-icon.rc`，将 `media/k380-fn-autolock-logo.ico` 嵌入程序。构建产物位于 `build/`：

- `k380FnAutoLock.exe`：后台自动恢复程序
- `setFnKeys.exe`：手动设置 F1–F12 优先
- `setMediaKeys.exe`：手动设置媒体键优先
- `hidapi.dll`：与构建架构对应的 HIDAPI 动态库

`startup.ps1` 是本地安装辅助脚本：它将后台程序、DLL 和 HIDAPI 授权文本复制到 `installed/`，添加当前用户开机启动项并启动程序。运行 `powershell -ExecutionPolicy Bypass -File .\startup.ps1 -Remove` 可移除启动项并关闭该脚本安装的程序。
