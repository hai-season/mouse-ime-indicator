#pragma once
#ifndef IMIND_IME_MONITOR_H
#define IMIND_IME_MONITOR_H

#include <windows.h>
#include "app.h"

namespace imeind {

// 一次查询的原始诊断数据（用于日志）
struct ImeDebug {
    HWND fg = nullptr;      // 前台顶层窗口
    HWND focus = nullptr;   // 前台线程实际焦点窗口
    HWND imeWnd = nullptr;  // 前台线程的默认 IME 窗口（WM_IME_CONTROL 通道）
    DWORD tid = 0;
    HKL hkl = nullptr;
    bool zhLayout = false;
    // 通道 A：WM_IME_CONTROL 直接询问 IME 窗口（im-select 同款，绕过 TSF 兼容层）
    LRESULT openA = 0;
    LRESULT convA = 0;
    // 通道 B：IMM32 兼容层
    HIMC himc = nullptr;
    BOOL immRet = FALSE;
    bool imeOpen = false;
    DWORD conv = 0;
    DWORD sent = 0;
};

// 输入法状态监听：
//  - WinEvent 跟踪前台窗口（EVENT_SYSTEM_FOREGROUND）
//  - IMM (ImmGetConversionStatus) 读取中/英模式
//  - WH_KEYBOARD_LL 捕获 CapsLock 按下（中文输入法会消费 CapsLock 做 中/英 切换并清除
//    真实切换态，故按下后进入短暂"稳定窗口"冻结大写显示，窗口结束再读真实状态）
//  - 兜底轮询只做免挂接的状态查询：读前台线程大写态需要 AttachThreadInput，
//    周期性挂接前台线程（如 Explorer）输入队列会吞掉桌面图标双击，
//    因此 attach 仅由前台切换事件和按键后的去抖/稳定定时器触发。
class ImeMonitor {
public:
    bool Start(HWND notifyWindow);
    void Stop();
    void Refresh();                    // 完整重查（含挂接输入队列读大写态），仅限事件触发
    const ImeState& GetState() const { return state_; }
    // 最后一次键盘输入的系统 tick（闲置判定用：仅统计键盘，鼠标移动不算）
    DWORD LastKeyTick() const { return lastKeyTick_; }

private:
    HWND notify_ = nullptr;
    HWINEVENTHOOK winEvent_ = nullptr;
    HHOOK kbHook_ = nullptr;
    HHOOK mouseHook_ = nullptr;      // 只读观察鼠标时刻，用于把 attach 推迟到鼠标空闲
    UINT_PTR timerId_ = 0;
    bool attachPending_ = false;     // 有一次"需要 attach 的大写校准"在等鼠标空闲
    mutable DWORD lastMouseTick_ = 0;
    ImeState state_;
    mutable bool capsOn_ = false;       // 内部跟踪的 CapsLock 切换态
    mutable bool capsDown_ = false;     // CapsLock 按下沿跟踪（keyup 复位，防重复触发）
    mutable DWORD capsPressTick_ = 0;   // 上次 CapsLock 按下时刻：宽限期内不重读大写态（避开输入法消费的瞬态）
    mutable int capsDiffRun_ = 0;       // 校准值与显示值连续不一致计数（≥2 才采纳，防闪烁）
    mutable int pollCount_ = 0;      // 轮询计数（用于心跳日志）
    mutable DWORD lastKeyTick_ = 0;  // 最后键盘输入 tick（钩子维护）

    void RefreshEx(bool attachCaps);   // attachCaps=false 时不碰输入队列（轮询用）
    void RequestAttachRefresh();       // 申请一次 attach 校准，等鼠标空闲再执行
    void Query(ImeState& out, ImeDebug& dbg, bool attachCaps) const;
    bool ReadCapsState(HWND fg, bool attachCaps) const;

    static LRESULT CALLBACK MouseHookProc(int code, WPARAM wParam, LPARAM lParam);

    static void CALLBACK WinEventProc(HWINEVENTHOOK hook, DWORD event, HWND hwnd,
                                      LONG idObject, LONG idChild, DWORD eventThread, DWORD time);
    static LRESULT CALLBACK KbHookProc(int code, WPARAM wParam, LPARAM lParam);
    static void CALLBACK PollTimer(HWND hwnd, UINT msg, UINT_PTR id, DWORD time);
};

// 全局唯一实例（main.cpp 定义）。必须声明为对象而非指针，
// 否则回调里按指针解引用会导致访问违例闪退。
extern ImeMonitor g_imeMonitor;

} // namespace imeind

#endif // IMIND_IME_MONITOR_H
