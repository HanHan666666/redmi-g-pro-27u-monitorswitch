# MonitorSwitch for Windows

Redmi 显示器 G Pro 27U 的 Windows 托盘控制工具：切换信号源（DP / HDMI 1 / HDMI 2 / USB-C）、调节背光与音量——全部走无线 ADB，显示器端无需安装任何应用。

这是 macOS 版的原生复刻，使用 **C++ / C++/WinRT + WinUI 3**，以桌面右下角托盘图标 + 弹出面板的形式运行。

## 功能对照

| 功能 | macOS 版 | Windows 版 |
|---|---|---|
| 托盘 / 菜单栏图标状态 | ✅ | ✅ 动态绘制，随任务栏明暗反色 |
| 弹出面板 | MenuBarExtra(.window) | WinUI 3 无边框窗口，贴托盘图标弹出 |
| 信号源切换 + 当前源打勾 | ✅ | ✅ |
| 背光 1–100（寄存器 → 广播 → 记账） | ✅ | ✅ |
| 音量 0–100（VolDirectTool 直写 + keyevent 回退） | ✅ | ✅ |
| 静音 | ✅ | ✅ |
| 10s 轮询 + 自动重连 | ✅ | ✅ |
| 首次连接部署 jar + 读寄存器校准背光 | ✅ | ✅ |
| ADB 地址设置（自动补 `:5555`） | ✅ | ✅ |
| 配置存储 | UserDefaults | `HKCU\Software\Wuhan\MonitorSwitch` |
| 单实例 | 系统保证 | 命名互斥量 + 托盘窗口消息 |
| 托盘右键菜单 | — | 打开面板 / 刷新 / ADB 设置… / 退出 |

## 要求

- Windows 10 1809（17763）或更高 / Windows 11，x64
- 显示器已开启「网络 ADB 调试」，与电脑同一局域网
- 装有 adb（Android platform-tools）。程序依次查找：exe 同目录、PATH、`%LOCALAPPDATA%\Android\Sdk\platform-tools`、WinGet Links、Scoop（shims / android-clt / android-platform-tools）、Chocolatey、`C:\platform-tools`、`C:\Android\platform-tools`

## 下载安装

从 [GitHub Releases](https://github.com/HanHan666666/redmi-g-pro-27u-monitorswitch/releases) 下载 `MonitorSwitch-*-Windows-x64-Setup.exe`，按向导安装即可。程序安装到当前用户的 `%LOCALAPPDATA%\Programs\MonitorSwitch`，无需管理员权限；可通过开始菜单打开，在 Windows「已安装的应用」中卸载。

便携版为 `MonitorSwitch-*-Windows-x64.zip`，解压后运行 `MonitorSwitch.exe`。安装包和便携包均附带 Windows App SDK 与 Visual C++ 运行库，不需要另装这两个运行库。ADB 仍需单独安装。

升级或卸载前，请从托盘右键菜单退出 MonitorSwitch。卸载保留显示器地址配置，重新安装后可继续使用。

## 从源码构建

需要 Visual Studio 2022（「使用 C++ 的桌面开发」工作负载）+ Windows 11 SDK 10.0.22621。首次构建会从 nuget.org 还原 Windows App SDK 1.8（约 300 MB，仅一次）。

```powershell
cd windows
.\build.ps1                    # Release x64
.\build.ps1 -Configuration Debug
```

产物在 `windows\build\<Configuration>\`，包含 Windows App SDK 运行时。用于分发时请运行打包脚本，以一并附带 Visual C++ 运行库。

安装 [Inno Setup 6.3+](https://jrsoftware.org/isdl.php)，使用 PowerShell 7 打包：

```powershell
.\package.ps1                    # 读取 VERSION 中的版本号
.\package.ps1 -Version 1.1.0
```

`windows\dist\` 会生成安装程序、便携 ZIP 和 SHA-256 校验文件。`-IsccPath` 可指定 `ISCC.exe` 的路径。

## 自动打包与发布

`.github/workflows/release-windows.yml` 在推送 `main`、Windows 相关 PR、手动运行时自动构建并上传安装包。非标签构建的包版本带 `-dev.<run number>`。流程会实际测试安装、覆盖安装、WinUI 面板启动与卸载。

发布 Windows 版本时推送标签：

```powershell
git tag windows-v1.1.0
git push origin windows-v1.1.0
```

构建与安装测试通过后，安装包、便携包和校验文件会自动出现在 GitHub Releases。后续版本更新 `VERSION`，并使用新的标签；无需修改工作流。

应用图标：

```powershell
python tools\make_icon.py      # 生成 Assets\AppIcon.ico（仓库已包含，改图标时才需要）
```

## 使用

1. 显示器上开启「网络 ADB 调试」（设置 → 账号与安全 → ADB 调试）
2. 首次启动自动弹出 ADB 设置窗口，填入显示器地址（如 `192.168.1.100:5555`，端口可省略）
3. 显示器首次连接会弹出授权，用遥控器选「始终允许」
4. 左键托盘图标开/合面板；右键托盘图标有快捷菜单

## 实现说明

- **Adb.cpp**：`CreateProcessW` + `CREATE_NO_WINDOW` + stdout/stderr 双管道并发读取（避免 64KB 管道写满死锁）+ 超时强杀；所有设备 I/O 用互斥量串行化，等价于 macOS 版的串行 `DispatchQueue`。`tvServiceRun` 保留 `${IFS}` 字面量编码约定。
- **Device.cpp**：Mtk（寄存器背光）与 VolDirect（音量直写）通道与 macOS 版逐条对齐；jar 从 exe 同目录（或 `Assets\` 子目录）push 到设备。
- **AppState.cpp**：状态只由 UI 线程读写；后台线程只拿快照，结果经 `DispatcherQueue` 回投。音量/背光的 hold 时间窗在松手后短时间内忽略轮询回写，防止滑块被设备旧值拉回。
- **TrayIcon.cpp**：`Shell_NotifyIcon` + 隐藏消息窗口；图标用 GDI+ 以 32bpp ARGB 绘制（display / display-slash / ? / !），响应 `TaskbarCreated` 与主题切换。
- **MainWindow**：`OverlappedPresenter` 去边框/标题栏、置顶、不进 Alt+Tab；Win11 用 `DWMWA_WINDOW_CORNER_PREFERENCE` 圆角，Win10 用 `SetWindowRgn` 兜底；面板位置由 `Shell_NotifyIconGetRect` 计算，点击别处（窗口失活）自动收起。
- **App**：纯 C++ 界面通过 `IXamlMetadataProvider` 转交 WinUI 控件元数据，并合并 `XamlControlsResources`，使用系统 Fluent 控件模板。
- **SettingsWindow**：原生 WinUI 3 `TextBox` + `InfoBar` + 强调按钮，支持选择、复制粘贴、输入法、回车保存 / Esc 取消；校验 IPv4 地址和端口，保存失败时就地提示。
- **布局**：托盘面板采用快捷设置式信号源磁贴；背光与音量滑块对齐，左侧扬声器按钮用于静音。窗口按客户区测量和缩放，避免底栏被非客户区边框裁切。
- **窗口边缘与位置**：整个面板使用客户区绘制，Windows 11 的圆角和阴影由 DWM 统一处理。打开时按托盘所在屏幕的任务栏边缘定位，并固定本次显示的锚点；展开或收起隐藏图标抽屉、轮询刷新、操作滑块都不会让面板跟随图标跳动。
- **背景材质**：面板与设置窗口使用系统 `DesktopAcrylicBackdrop`；内容背景透明，保留系统亚克力的模糊、透色和噪点，面板底栏仅增加轻微分层。材质深浅与关闭透明效果时的纯色回退由系统管理。
- **常驻开销**：仅在首次打开窗口时加载 Fluent 样式和面板，纯托盘启动不创建界面；隐藏时不刷新界面布局，重新打开时同步设备状态。托盘状态和提示未变化时不重复绘制图标。首次使用界面后，WinUI 框架和图形缓存仍会驻留。
