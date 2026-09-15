#pragma once
#ifndef IMIND_APP_H
#define IMIND_APP_H

// GDI+ 与 windows.h 的 min/max 宏兼容（必须先于 windows.h 定义）
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <string>

namespace imeind {

// 输入法状态（监听层产出，UI 层消费）
struct ImeState {
    bool chinese = false;         // 中文输入法且处于中文模式
    bool englishLayout = true;    // 当前键盘布局为英文（显示 EN）
    bool capsLock = false;        // CapsLock 切换态
    std::wstring langText;        // L"中" / L"英" / L"EN"

    bool operator!=(const ImeState& o) const {
        return chinese != o.chinese || englishLayout != o.englishLayout || capsLock != o.capsLock;
    }
};

// 悬浮窗显示策略
enum class ShowPolicy : int {
    Always = 0,     // 始终显示
    InputFocus = 1, // 仅输入框聚焦时显示
    IdleHide = 2,   // 闲置 N 秒后自动隐藏
};

// ---- 特效配置：每个特效一个结构体，新增特效 = 新增一个 Fx 结构体 + 设置页对应区块 ----

// 圆圈特效：悬浮窗本体即圆圈；禁用时窗口变全透明，仅作特效锚点跟随光标
struct CircleFxCfg {
    bool enabled = true;      // 是否绘制圆圈本体（显隐时机仍由「显示策略」驱动）
    int size = 16;            // 圆圈直径（像素）
    int borderWidth = 1;      // 描边宽度（像素，0 = 无描边）
    int opacity = 100;        // 全局不透明度（%，20-100）；特效窗亮度跟随此值
    int colorZh = 0x2E7D5A;   // 圆圈颜色：中文模式（0xRRGGBB）
    int colorYing = 0x42464E; // 圆圈颜色：英文（英，非英文键盘）
    int colorEn = 0x363A42;   // 圆圈颜色：EN（英文键盘布局）
    int colorCaps = 0xC63A2E; // 圆圈颜色：大写锁定
};

// 特效按状态配色：mode 0=不显示 1=彩色 2=纯色；color 仅纯色模式使用（0xRRGGBB）
struct FxStateCfg {
    int mode = 0;
    int color = 0;
};

// 烟花特效：圆圈持续绽放烟花下落（中文/英文/大写三态各自配置）
struct FireworksFxCfg {
    bool enabled = false;
    FxStateCfg zh{1, 0xFFD24A};   // 中文：默认彩色（与旧版行为一致）
    FxStateCfg en{0, 0x4FC3F7};   // 英文：默认不显示
    FxStateCfg caps{0, 0xFF5252}; // 大写：默认不显示
};

// 沙粒特效：圆圈下方缓缓漏落沙粒（三态配置同上）
struct SandFxCfg {
    bool enabled = true;
    int spawnPerTick = 2; // 每帧（33ms）生成的沙粒数（1-8）
    int tiltMax = 0;      // 初始方向随机倾斜角上限（度，0-45；0=垂直，每粒 0~上限 随机、左右随机）
    FxStateCfg zh{1, 0xD8B26A};
    FxStateCfg en{0, 0x4FC3F7};
    FxStateCfg caps{0, 0xFF5252};
};

struct Config {
    ShowPolicy policy = ShowPolicy::Always;
    int offsetX = 12;         // 相对光标 X 偏移（像素）
    int offsetY = 10;         // 相对光标 Y 偏移（像素）
    bool autostart = false;   // 开机自启
    int idleSeconds = 30;     // 闲置隐藏秒数（policy == IdleHide 时生效）
    CircleFxCfg circle;
    FireworksFxCfg fireworks;
    SandFxCfg sand;
};

// 全局共享上下文
struct App {
    HINSTANCE hInst = nullptr;
    HWND hMain = nullptr;    // 隐藏主窗口（托盘宿主 / 消息中枢）
    HWND hFloat = nullptr;   // 悬浮窗
    HWND hFx = nullptr;      // 烟花特效窗
    HWND hSand = nullptr;    // 沙粒特效窗
    Config cfg;
    ImeState state;
    HICON hTrayIcon = nullptr; // 当前托盘图标
};

extern App g_app;

// 自定义消息
constexpr UINT WM_APP_IME_STATE       = WM_APP + 1; // 输入法状态已变化
constexpr UINT WM_APP_CONFIG_APPLIED  = WM_APP + 3; // 配置已应用
constexpr UINT WM_APP_SHOW            = WM_APP + 5; // 显示悬浮窗
constexpr UINT WM_APP_TRAY            = WM_APP + 6; // 托盘通知
constexpr UINT WM_APP_EXIT            = WM_APP + 7; // 退出

} // namespace imeind

#endif // IMIND_APP_H
