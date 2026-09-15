# 输入法指示器（ImeIndicator）

[![license: MIT](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
![platform](https://img.shields.io/badge/platform-Windows%2010%2B-blue.svg)
![C++](https://img.shields.io/badge/C%2B%2B-17-blue.svg)
[![build](https://github.com/hai-season/mouse-ime-indicator/actions/workflows/build.yml/badge.svg)](https://github.com/hai-season/mouse-ime-indicator/actions/workflows/build.yml)

跟随鼠标的输入法状态悬浮窗：以**颜色编码的圆圈**实时指示 中 / 英 / EN 与 **大小写** 状态。
单文件 exe、无运行时依赖、空闲 CPU ≈ 0。

## 下载

从 [Releases](https://github.com/hai-season/mouse-ime-indicator/releases) 下载 `ImeIndicator.exe`，双击即用（绿色免安装）。

## 预览

<!-- 发布前取消注释，并把素材放进 docs/imgs/：
     circle.gif = 圆圈跟随光标 + 切换 中/英/大写 + 特效；settings.png = 设置面板
| 状态圆圈与特效 | 设置面板 |
|---|---|
| ![preview](docs/imgs/circle.gif) | ![settings](docs/imgs/settings.png) |
-->

## 功能

- 悬浮框附着在鼠标右下方，实时跟随（33ms 跟随循环，空闲零重绘）
- 显示：圆形状态点，颜色编码状态（绿=中文 / 暗灰=英 / 更深=EN / 红=大写锁定），大小与描边可配置
- 状态监听：前台窗口跟踪（WinEvent）+ IMM 中英模式 + CapsLock 即时钩子 + 1s 兜底轮询，事件驱动、仅变化时刷新
- 托盘：左键显隐悬浮窗，右键菜单（设置 / 开机自启 / 退出），托盘图标随中英状态变色
- 设置：显示策略（始终显示 / 仅输入框聚焦时 / 闲置自动隐藏）、位置偏移、圆圈外观（大小 / 描边 / 透明度 / 状态颜色）、特效（烟花 / 沙粒，按 中/英/大写 三态分别配置）、开机自启
- 配置存于 `%APPDATA%\ImeIndicator\config.json`
- 单实例：重复启动时唤出已有悬浮窗

## 构建

需要 MSVC（Visual Studio Build Tools）或 MinGW-w64 g++，二者自动探测。

**推荐（直接双击）**：
```
build.bat
```

或在 PowerShell 中执行：
```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File build.ps1
```

产物：`ImeIndicator.exe`（无运行时依赖，直接运行）。构建日志：`build/build.log`。

注意：
- `build.bat` 会**结束正在运行的旧实例**以便覆盖 exe，构建成功后自动启动新版本；不需要自动启动用 `build.bat -NoRun`
- `build.ps1` 不杀进程、不启动程序，适合脚本 / CI 调用（GitHub Actions 见 [.github/workflows/build.yml](.github/workflows/build.yml)，推送 `v*` tag 自动发布 Release）
- 两个脚本均为纯 ASCII 编写，避免中文 Windows（GBK 代码页）下乱码导致的解析失败；源码按 UTF-8 编译（MSVC `/utf-8`）

## 使用

- 双击 `ImeIndicator.exe` 运行，悬浮框出现在鼠标旁
- 右键托盘图标 → 设置
- 退出：托盘右键 → 退出

## 发布新版本

1. 更新 [res/version.rc](res/version.rc) 中的 `VER_MAJOR / VER_MINOR / VER_PATCH`
2. `git commit` 后打 tag：`git tag vX.Y.Z && git push origin vX.Y.Z`
3. CI 自动构建并把 exe 附加到 GitHub Release

## 隐私

- 完全本地运行：无网络请求、无遥测、不收集上传任何数据
- 键盘钩子仅读取 CapsLock 键状态与最后按键时刻（闲置隐藏判定用），不记录按键内容
- 配置与日志仅写入本机 `%APPDATA%\ImeIndicator\`，可随时删除

## 技术要点

| 项 | 实现 |
|---|---|
| 悬浮窗 | `WS_EX_LAYERED + TOPMOST + NOACTIVATE + TOOLWINDOW + TRANSPARENT`（点击穿透固定开启），GDI+ 双缓冲绘制，`UpdateLayeredWindow` 输出 |
| 中英判定 | `GetKeyboardLayout` 语言 + 双通道读 `IME_CMODE_NATIVE`：`WM_IME_CONTROL/IMC_GETCONVERSIONMODE` 直询 IME 窗口（绕过 IMM），IMM32 `ImmGetConversionStatus` 兜底 |
| 大小写 | `WH_KEYBOARD_LL` 按下沿即时翻转显示；稳定期后用 `AttachThreadInput + GetKeyboardState` 校准真实切换态（`GetAsyncKeyState` 对微软拼音不可靠；挂接推迟到鼠标空闲，防吞桌面双击） |
| 性能 | 空闲 CPU ≈ 0–1%，内存 < 15MB，事件驱动 + 1s 免挂接兜底轮询 |

## 已知限制

- 中/英模式主通道为 `WM_IME_CONTROL` 直询 IME 窗口、IMM 兼容层兜底，对绝大多数中文输入法（微软拼音、搜狗、QQ 等）有效；个别纯 TSF 输入法若上报异常，可在此处扩展 TSF compartment 通道（`GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION`）
- 「仅输入框聚焦时显示」为启发式判定（按控件类名匹配常见输入控件）
- 跨平台预留：`ImeMonitor` 为平台层，Linux 可实现 IBus/Fcitx5 DBus + XKB 的同接口实现

## 目录结构

```
src/               源码（说明见上「技术要点」）
res/
  app.manifest     ComCtl32 v6 视觉样式 manifest
  app.rc           MSVC 资源脚本（manifest + 版本信息）
  version.rc       exe 版本信息（发版时改这里）
build.ps1          构建脚本（自动探测 MSVC / MinGW，不产生副作用）
build.bat          便捷封装：杀旧实例 → 构建 → 启动（-NoRun 跳过启动）
.github/workflows/ CI：push 构建验证，tag 自动发 Release
docs/imgs/         README 截图 / 录屏
```
