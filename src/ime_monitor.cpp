#include "ime_monitor.h"

#include <windows.h>
#include <imm.h> // ImmGetContext / ImmGetConversionStatus 等
#include "log.h"

// MinGW-w64 的 imm.h 缺少以下 WM_IME_CONTROL 常量（MS SDK 值：1 / 5）
#ifndef IMC_GETCONVERSIONMODE
#define IMC_GETCONVERSIONMODE 0x0001
#endif
#ifndef IMC_GETOPENSTATUS
#define IMC_GETOPENSTATUS 0x0005
#endif

namespace imeind {

// CapsLock 按下后保持钩子翻转值、暂不重读系统状态的时长：仅覆盖按键瞬间切换态
// 尚未落定的窗口（实测 <300ms）。
static constexpr DWORD kCapsSettleMs = 600;
// 按键后去抖：敲键停止 150ms 才做一次完整重查（Shift 切中英等无独立事件源的场景）
static constexpr UINT kKeyDebounceMs = 150;
// 兜底轮询：仅做免挂接查询（语言栏鼠标点击切中英等），绝不 AttachThreadInput
static constexpr UINT kPollMs = 1000;

// 鼠标事件后保持不 attach 输入队列的空闲窗口：双击桌面图标时，第 1 击常触发前台切换
// 事件，若此时立即 AttachThreadInput 会打断两击的双击配对，导致第一次双击失效。
static constexpr DWORD kMouseIdleMs = 400;

// notify_ 上的定时器 id
static constexpr UINT_PTR kTimerPoll = 1;   // 周期兜底（不 attach）
static constexpr UINT_PTR kTimerKey = 2;    // 按键去抖 one-shot
static constexpr UINT_PTR kTimerCaps = 3;   // CapsLock 稳定期结束 one-shot
static constexpr UINT_PTR kTimerGate = 4;   // attach 校准门控：等鼠标空闲再 attach

bool ImeMonitor::Start(HWND notifyWindow) {
    notify_ = notifyWindow;
    winEvent_ = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
                                nullptr, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);
    kbHook_ = SetWindowsHookExW(WH_KEYBOARD_LL, KbHookProc, GetModuleHandleW(nullptr), 0);
    mouseHook_ = SetWindowsHookExW(WH_MOUSE_LL, MouseHookProc, GetModuleHandleW(nullptr), 0);
    timerId_ = SetTimer(notify_, kTimerPoll, kPollMs, PollTimer);
    LogMsg(L"monitor start: winEvent=%s kbHook=%s mouseHook=%s timer=%s",
           winEvent_ ? L"ok" : L"FAIL", kbHook_ ? L"ok" : L"FAIL",
           mouseHook_ ? L"ok" : L"FAIL", timerId_ ? L"ok" : L"FAIL");
    lastKeyTick_ = GetTickCount(); // 闲置判定初始时间
    capsOn_ = ReadCapsState(GetForegroundWindow(), true);
    LogMsg(L"monitor start: capsOn=%d", (int)capsOn_);
    Refresh();
    return true;
}

void ImeMonitor::Stop() {
    LogMsg(L"monitor stop");
    if (winEvent_) { UnhookWinEvent(winEvent_); winEvent_ = nullptr; }
    if (kbHook_) { UnhookWindowsHookEx(kbHook_); kbHook_ = nullptr; }
    if (mouseHook_) { UnhookWindowsHookEx(mouseHook_); mouseHook_ = nullptr; }
    if (notify_) {
        KillTimer(notify_, kTimerPoll);
        KillTimer(notify_, kTimerKey);
        KillTimer(notify_, kTimerCaps);
        KillTimer(notify_, kTimerGate);
    }
    timerId_ = 0;
}

void CALLBACK ImeMonitor::WinEventProc(HWINEVENTHOOK, DWORD, HWND hwnd, LONG, LONG, DWORD, DWORD) {
    // 前台窗口切换 → 立即重查（该回调由本线程消息泵分发）
    wchar_t cls[96] = {0};
    if (hwnd) GetClassNameW(hwnd, cls, 96);
    LogMsg(L"event: foreground changed hwnd=%p cls=%ls", hwnd, cls);
    // 语言显示立即更新（免挂接）；大写校准推迟到鼠标空闲，避免打断正在进行的双击
    g_imeMonitor.RefreshEx(false);
    g_imeMonitor.RequestAttachRefresh();
}

LRESULT CALLBACK ImeMonitor::KbHookProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code >= 0 && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN ||
                      wParam == WM_KEYUP || wParam == WM_SYSKEYUP)) {
        KBDLLHOOKSTRUCT* k = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        if (k && (k->flags & LLKHF_UP) == 0) {
            // 任意键盘按下都刷新"最后按键时间"（闲置判定用）
            g_imeMonitor.lastKeyTick_ = GetTickCount();
            // 敲键停止后再做一次完整重查（去抖，避免每个按键都跨线程查询）
            if (g_imeMonitor.notify_) SetTimer(g_imeMonitor.notify_, kTimerKey, kKeyDebounceMs, PollTimer);
        }
        if (k && k->vkCode == VK_CAPITAL) {
            bool down = (k->flags & LLKHF_UP) == 0;
            if (down && !g_imeMonitor.capsDown_) {
                // 按下沿立即翻转显示（即时反馈）。宽限期内 ReadCapsState 保持该值，
                // 期满由 kTimerCaps 做一次 attach 校准真实状态。
                // 说明：若把"中/英切换键"设成 CapsLock，输入法会消费该键并在 ~1s 后撤销
                // 大写锁定，此时校准会把显示纠正回来（圆圈变回中/英，属正确行为）。
                g_imeMonitor.capsDown_ = true;
                g_imeMonitor.capsOn_ = !g_imeMonitor.capsOn_;
                g_imeMonitor.capsPressTick_ = GetTickCount();
                g_imeMonitor.capsDiffRun_ = 0;
                LogMsg(L"hook: CapsLock keydown -> caps=%d (grace %dms)", (int)g_imeMonitor.capsOn_,
                       (int)kCapsSettleMs);
                if (g_imeMonitor.notify_) {
                    SetTimer(g_imeMonitor.notify_, kTimerCaps, kCapsSettleMs, PollTimer);
                    // 免挂接广播翻转结果（0ms 一次性）：钩子内绝不跨线程 SendMessageTimeout——
                    // 超过 LowLevelHooksTimeout 会被系统静默摘钩，还会卡全系统输入
                    SetTimer(g_imeMonitor.notify_, kTimerKey, 0, PollTimer);
                }
            } else if (!down) {
                g_imeMonitor.capsDown_ = false;
            }
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

void CALLBACK ImeMonitor::PollTimer(HWND hwnd, UINT, UINT_PTR id, DWORD) {
    switch (id) {
    case kTimerKey:
        KillTimer(hwnd, kTimerKey);
        g_imeMonitor.RefreshEx(false);       // Shift 切中英：免挂接查询即可感知
        g_imeMonitor.RequestAttachRefresh(); // 大写真值仍等鼠标空闲校准
        break;
    case kTimerCaps:
        KillTimer(hwnd, kTimerCaps);
        g_imeMonitor.RequestAttachRefresh();
        break;
    case kTimerGate: {
        if (!g_imeMonitor.attachPending_) { KillTimer(hwnd, kTimerGate); break; }
        if (GetTickCount() - g_imeMonitor.lastMouseTick_ >= kMouseIdleMs) {
            g_imeMonitor.attachPending_ = false;
            KillTimer(hwnd, kTimerGate);
            g_imeMonitor.RefreshEx(true);
            // 防闪烁采用连续两次一致才采纳：若第一次校准记了差值，再来一次
            if (g_imeMonitor.capsDiffRun_ > 0) g_imeMonitor.RequestAttachRefresh();
        } else {
            SetTimer(hwnd, kTimerGate, 200, PollTimer); // 鼠标还在动/刚点完，继续等
        }
        break;
    }
    default:
        // 兜底轮询：语言栏鼠标点击等事件源未覆盖的场景；不碰输入队列，
        // 周期性 AttachThreadInput 会吞掉桌面图标双击
        g_imeMonitor.RefreshEx(false);
        break;
    }
}

void ImeMonitor::RequestAttachRefresh() {
    attachPending_ = true;
    if (notify_) SetTimer(notify_, kTimerGate, 200, PollTimer);
}

LRESULT CALLBACK ImeMonitor::MouseHookProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code >= 0) {
        // 仅记录时刻：任何鼠标活动后 kMouseIdleMs 内禁止 AttachThreadInput
        g_imeMonitor.lastMouseTick_ = GetTickCount();
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

void ImeMonitor::Refresh() {
    RefreshEx(true);
}

void ImeMonitor::RefreshEx(bool attachCaps) {
    ImeState next;
    ImeDebug dbg;
    Query(next, dbg, attachCaps);
    bool changed = next != state_;
    if (changed) {
        state_ = next;
        LogMsg(L"state change: lang=%ls caps=%d (fg=%p focus=%p imeWnd=%p tid=%u hkl=%p zh=%d "
               L"| A: openA=%lld convA=0x%llX | B: himc=%p immRet=%d imeOpen=%d conv=0x%X sent=0x%X)",
               next.langText.c_str(), (int)next.capsLock,
               dbg.fg, dbg.focus, dbg.imeWnd, dbg.tid, (void*)dbg.hkl, (int)dbg.zhLayout,
               (long long)dbg.openA, (long long)dbg.convA,
               dbg.himc, (int)dbg.immRet, (int)dbg.imeOpen, dbg.conv, dbg.sent);
        if (notify_) PostMessageW(notify_, WM_APP_IME_STATE, 0, 0);
    } else if (++pollCount_ % 30 == 0) {
        // 心跳：状态未变也要定期输出原始数据，便于排查"读不到"的问题
        LogMsg(L"heartbeat: lang=%ls caps=%d (fg=%p focus=%p imeWnd=%p tid=%u hkl=%p zh=%d "
               L"| A: openA=%lld convA=0x%llX | B: himc=%p immRet=%d imeOpen=%d conv=0x%X sent=0x%X)",
               next.langText.c_str(), (int)next.capsLock,
               dbg.fg, dbg.focus, dbg.imeWnd, dbg.tid, (void*)dbg.hkl, (int)dbg.zhLayout,
               (long long)dbg.openA, (long long)dbg.convA,
               dbg.himc, (int)dbg.immRet, (int)dbg.imeOpen, dbg.conv, dbg.sent);
    }
}

bool ImeMonitor::ReadCapsState(HWND fg, bool attachCaps) const {
    // 轮询路径不挂接前台线程输入队列，保持已知值（变化由按键/前台切换事件驱动校准）
    if (!attachCaps) return capsOn_;
    // 宽限期内保持钩子按下沿的翻转值，避开按键瞬间系统切换态尚未落定的读取竞争。
    if (GetTickCount() - capsPressTick_ < kCapsSettleMs) {
        return capsOn_;
    }
    // 真值来源：前台线程键盘状态（应用真正用来决定大小写的状态）。
    // 实测微软拼音会把 GetAsyncKeyState 的全局切换位留成 0，而键盘灯亮、实际打出大写，
    // 因此不能用 GetAsyncKeyState 校准。
    bool on = false;
    bool got = false;
    DWORD fgTid = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    bool attached = false;
    if (fgTid && fgTid != GetCurrentThreadId()) {
        attached = AttachThreadInput(GetCurrentThreadId(), fgTid, TRUE) != FALSE;
    }
    BYTE ks[256] = {0};
    if (GetKeyboardState(ks)) {
        on = (ks[VK_CAPITAL] & 1) != 0;
        got = true;
    }
    if (attached) {
        AttachThreadInput(GetCurrentThreadId(), fgTid, FALSE);
    }
    // 挂接/读取失败（安全桌面、权限差异等）：保持上一个值，不退回不可信的 async 切换位
    if (!got) return capsOn_;
    // 前台线程切换态在按键后短暂仍为旧值（输入队列处理有延迟），
    // 要求连续两次读到一致的新值才采纳，避免单次中间值造成 红→灰→红 闪烁。
    if (on == capsOn_) {
        capsDiffRun_ = 0;
    } else if (++capsDiffRun_ >= 2) {
        capsOn_ = on;
        capsDiffRun_ = 0;
    }
    return capsOn_;
}

void ImeMonitor::Query(ImeState& out, ImeDebug& dbg, bool attachCaps) const {
    dbg.fg = GetForegroundWindow();
    DWORD tid = dbg.fg ? GetWindowThreadProcessId(dbg.fg, nullptr) : 0;
    dbg.tid = tid;
    HKL hkl = tid ? GetKeyboardLayout(tid) : GetKeyboardLayout(0);
    dbg.hkl = hkl;
    LANGID lang = (LANGID)((ULONG_PTR)hkl & 0xFFFF);
    bool zhLayout = (PRIMARYLANGID(lang) == LANG_CHINESE);
    dbg.zhLayout = zhLayout;

    // 焦点窗口（输入上下文挂在其上），失败回退顶层窗口
    HWND immWnd = dbg.fg;
    if (dbg.fg && tid) {
        GUITHREADINFO gti{};
        gti.cbSize = sizeof(gti);
        if (GetGUIThreadInfo(tid, &gti) && gti.hwndFocus) {
            immWnd = gti.hwndFocus;
            dbg.focus = gti.hwndFocus;
        }
    }

    bool native = false;

    // 英文布局无需判中英，跳过通道 A/B 的两次跨线程查询（省掉每次轮询的 SendMessage 开销）
    if (zhLayout) {
        // 通道 A：直接问前台线程的默认 IME 窗口（绕过 IMM 兼容层，im-select 同款方案）
        HWND imeWnd = immWnd ? ImmGetDefaultIMEWnd(immWnd) : nullptr;
        dbg.imeWnd = imeWnd;
        if (imeWnd) {
            DWORD_PTR res = 0;
            if (SendMessageTimeoutW(imeWnd, WM_IME_CONTROL, IMC_GETCONVERSIONMODE, 0,
                                    SMTO_BLOCK | SMTO_ABORTIFHUNG, 150, &res)) {
                dbg.convA = (LRESULT)res;
                if (dbg.convA & IME_CMODE_NATIVE) native = true;
            }
            res = 0;
            SendMessageTimeoutW(imeWnd, WM_IME_CONTROL, IMC_GETOPENSTATUS, 0,
                                SMTO_BLOCK | SMTO_ABORTIFHUNG, 150, &res);
            dbg.openA = (LRESULT)res;
        }

        // 通道 B：IMM32 兼容层（A 无数据时的兜底）
        if (immWnd) {
            HIMC himc = ImmGetContext(immWnd);
            dbg.himc = himc;
            if (himc) {
                dbg.imeOpen = ImmGetOpenStatus(himc) != FALSE;
                DWORD conv = 0, sent = 0;
                dbg.immRet = ImmGetConversionStatus(himc, &conv, &sent);
                dbg.conv = conv;
                dbg.sent = sent;
                if (!native && (conv & IME_CMODE_NATIVE)) native = true;
                ImmReleaseContext(immWnd, himc);
            }
        }
    }

    // 中文判定只看 IME_CMODE_NATIVE：TSF 输入法的 ImmGetOpenStatus 常不可靠
    out.englishLayout = !zhLayout;
    if (zhLayout && native) {
        out.chinese = true;
        out.langText = L"中";
    } else if (zhLayout) {
        out.chinese = false;
        out.langText = L"英";
    } else {
        out.chinese = false;
        out.langText = L"EN";
    }
    out.capsLock = ReadCapsState(dbg.fg, attachCaps);
    // 大写开启时输入的是大写英文字母，指示器按英文显示（避免误导）
    if (out.capsLock) {
        out.chinese = false;
        out.langText = out.englishLayout ? L"EN" : L"英";
    }
}

} // namespace imeind
