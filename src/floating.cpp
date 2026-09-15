#include "floating.h"
#include "ime_monitor.h" // g_imeMonitor.LastKeyTick()：闲置判定用键盘时间

#include <gdiplus.h>
#include <string>
#include "log.h"

using namespace Gdiplus;

namespace imeind {

namespace {

Color BorderColor() { return Color(90, 255, 255, 255); } // 半透明白描边（深色场景下也可见）

// 圆圈颜色编码状态（色值可在设置里自定义）：大写 / 中文 / 英 / EN
Color CircleColor() {
    const ImeState& s = g_app.state;
    auto fromRgb = [](int rgb) {
        return Color(255, (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
    };
    if (s.capsLock) return fromRgb(g_app.cfg.circle.colorCaps);
    if (s.chinese) return fromRgb(g_app.cfg.circle.colorZh);
    if (!s.englishLayout) return fromRgb(g_app.cfg.circle.colorYing);
    return fromRgb(g_app.cfg.circle.colorEn);
}

GraphicsPath* MakeRoundRect(float x, float y, float w, float h, float r) {
    GraphicsPath* p = new GraphicsPath();
    float d = r * 2.0f;
    p->AddArc(x, y, d, d, 180, 90);
    p->AddArc(x + w - d, y, d, d, 270, 90);
    p->AddArc(x + w - d, y + h - d, d, d, 0, 90);
    p->AddArc(x, y + h - d, d, d, 90, 90);
    p->CloseFigure();
    return p;
}

} // namespace

bool FloatingWindow::Create(HINSTANCE hInst, HWND owner) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"ImeIndicatorFloat";
    RegisterClassExW(&wc);

    // 点击穿透无条件强制：纯指示窗口必须永远不拦截鼠标（WS_EX_TRANSPARENT + 分层窗 = 系统完全忽略其鼠标输入）
    DWORD ex = WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT;
    hwnd_ = CreateWindowExW(ex, L"ImeIndicatorFloat", L"", WS_POPUP,
                            0, 0, 1, 1, owner, nullptr, hInst, this);
    if (!hwnd_) {
        LogMsg(L"float: CreateWindowEx FAILED err=%lu", GetLastError());
        return false;
    }

    SetTimer(hwnd_, 1, 33, nullptr);
    LogMsg(L"float: window created hwnd=%p", hwnd_);
    return true;
}

void FloatingWindow::Destroy() {
    if (hwnd_) {
        KillTimer(hwnd_, 1);
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

LRESULT CALLBACK FloatingWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    FloatingWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        self = reinterpret_cast<FloatingWindow*>(((CREATESTRUCTW*)lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = reinterpret_cast<FloatingWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    switch (msg) {
    case WM_TIMER:
        if (self) self->OnTimer();
        return 0;
    case WM_ERASEBKGND:
        return 1; // 分层窗口无需擦除背景
    case WM_NCHITTEST:
        // 点击穿透：永远返回 HTTRANSPARENT，鼠标事件绝不落在悬浮窗上
        return HTTRANSPARENT;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void FloatingWindow::OnTimer() {
    // 手动隐藏优先：用户通过托盘隐藏后保持隐藏，不遵循显示策略
    if (override_ == 0) {
        if (visible_) {
            visible_ = false;
            ShowWindow(hwnd_, SW_HIDE);
        }
        return;
    }

    bool should = (override_ == 1) ? true : ShouldShow();
    if (should != visible_) {
        visible_ = should;
        LogMsg(L"float: visibility -> %ls (policy=%d override=%d)", should ? L"show" : L"hide",
               (int)g_app.cfg.policy, override_);
        if (should) {
            placed_ = false;
            Draw(); // 先绘制再显示，避免空白
            ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        } else {
            ShowWindow(hwnd_, SW_HIDE);
            return;
        }
    }
    if (!visible_) return;

    // 跟随鼠标
    POINT p{};
    if (!GetCursorPos(&p)) return; // 无光标（锁屏等）时不动
    if (width_ <= 0 || height_ <= 0) return;

    int x = p.x + g_app.cfg.offsetX;
    int y = p.y + g_app.cfg.offsetY;

    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    HMONITOR mon = MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST);
    if (GetMonitorInfoW(mon, &mi)) {
        RECT wr = mi.rcWork;
        // 超出右/下边界时翻转到光标另一侧
        if (x + width_ > wr.right)  x = p.x - g_app.cfg.offsetX - width_;
        if (y + height_ > wr.bottom) y = p.y - g_app.cfg.offsetY - height_;
        if (x < wr.left) x = wr.left;
        if (y < wr.top)  y = wr.top;
    }

    if (!placed_ || x != lastX_ || y != lastY_) {
        lastX_ = x;
        lastY_ = y;
        placed_ = true;
        // 注意：不带 SWP_SHOWWINDOW —— 位置更新不得把隐藏窗口重新显示出来
        SetWindowPos(hwnd_, HWND_TOPMOST, x, y, 0, 0,
                     SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }
}

bool FloatingWindow::ShouldShow() const {
    ShowPolicy policy = g_app.cfg.policy;
    if (policy == ShowPolicy::Always) return true;

    if (policy == ShowPolicy::InputFocus) {
        // 启发式：前台线程焦点控件是否属于常见文本输入控件
        HWND fg = GetForegroundWindow();
        if (!fg) return false;
        DWORD tid = GetWindowThreadProcessId(fg, nullptr);
        GUITHREADINFO gti{};
        gti.cbSize = sizeof(gti);
        if (!GetGUIThreadInfo(tid, &gti) || !gti.hwndFocus) return false;

        wchar_t cls[96] = {0};
        GetClassNameW(gti.hwndFocus, cls, 96);
        std::wstring c = cls;
        static const wchar_t* keys[] = {
            // 原生控件
            L"Edit", L"RICH", L"Scintilla", L"ConsoleWindowClass",
            L"SearchBox", L"SearchTextBox", L"WindowsForms10.EDIT",
            L"AfxEdit", L"TEdit", L"TMemo", L"Notepad", L"OneNote", L"OpusApp",
            // Chromium/Electron/CEF：整个应用是单窗口，输入焦点留在顶层窗口
            L"Chrome_RenderWidgetHostHWND", L"Chrome_WidgetWin_0",
            L"Chrome_WidgetWin_1", L"CefBrowserWindow",
            // UWP / 微软系
            L"ApplicationFrameWindow", L"Windows.UI.Core.CoreWindow",
            L"DirectUIHWND",
            // Java (JetBrains/Eclipse 等)
            L"SunAwtFrame", L"SunAwtCanvas", L"SWT_Window0", L"SWT_Window1",
            // Qt 应用顶层窗口
            L"Qt5QWindowIcon", L"Qt6QWindowIcon", L"Qt5QLineEdit",
            // 浏览器整窗
            L"MozillaWindowClass",
        };
        for (const wchar_t* k : keys) {
            if (c.find(k) != std::wstring::npos) return true;
        }
        return false;
    }

    if (policy == ShowPolicy::IdleHide) {
        // 仅统计键盘输入：悬浮窗跟随鼠标，鼠标移动不应阻止隐藏
        DWORD now = GetTickCount();
        if (now - g_imeMonitor.LastKeyTick() >= (DWORD)g_app.cfg.idleSeconds * 1000) return false;
        return true;
    }
    return true;
}

void FloatingWindow::ComputeSize(const ImeState& s) {
    (void)s;
    width_ = g_app.cfg.circle.size;
    height_ = g_app.cfg.circle.size;
}

void FloatingWindow::Draw() {
    const ImeState& s = g_app.state;
    ComputeSize(s);
    if (width_ <= 0 || height_ <= 0) return;

    // 32 位 DIB（顶向下，含 alpha）
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width_;
    bi.bmiHeader.biHeight = -height_;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP hbmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!hbmp || !bits) {
        if (hbmp) DeleteObject(hbmp);
        return;
    }
    HDC memdc = CreateCompatibleDC(nullptr);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memdc, hbmp);

    {
        Graphics g(memdc);
        g.SetSmoothingMode(SmoothingModeAntiAlias);

        // 圆圈：状态色填充 + 半透明白描边（描边宽度可配置，0 = 无描边）
        // 禁用时不画任何像素 → DIB 全透明，窗口仍在：作为特效锚点跟随光标
        if (g_app.cfg.circle.enabled) {
            GraphicsPath* circle = MakeRoundRect(0, 0, (REAL)width_, (REAL)height_, (REAL)width_ / 2.0f);
            SolidBrush cb(CircleColor());
            g.FillPath(&cb, circle);
            if (g_app.cfg.circle.borderWidth > 0) {
                Pen ring(BorderColor(), (REAL)g_app.cfg.circle.borderWidth);
                g.DrawPath(&ring, circle);
            }
            delete circle;
        }
    }

    PremultiplyDib(bits, width_, height_);

    POINT src{0, 0};
    SIZE sz{width_, height_};
    // SourceConstantAlpha 与每像素 alpha 叠加：实现整体不透明度（%→0-255）
    BYTE alpha = (BYTE)(g_app.cfg.circle.opacity * 255 / 100);
    BLENDFUNCTION bf{AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA};
    // pptDst 必须为 NULL：非 NULL 会把分层窗口移动到指定位置（屏幕原点）
    BOOL ulwOk = UpdateLayeredWindow(hwnd_, nullptr, nullptr, &sz, memdc, &src, 0, &bf, ULW_ALPHA);
    if (!ulwOk) {
        LogMsg(L"float: UpdateLayeredWindow FAILED err=%lu size=%dx%d", GetLastError(),
               width_, height_);
    }

    // 尺寸变化时同步窗口大小（不重定位，位置由 OnTimer 控制）
    RECT rc{};
    GetWindowRect(hwnd_, &rc);
    if (rc.right - rc.left != width_ || rc.bottom - rc.top != height_) {
        SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, width_, height_,
                     SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }

    SelectObject(memdc, oldBmp);
    DeleteDC(memdc);
    DeleteObject(hbmp);
}

void FloatingWindow::UpdateState(const ImeState& s) {
    (void)s;
    if (visible_) Draw();
}

void FloatingWindow::ApplyConfig() {
    override_ = -1; // 配置变更后回到策略驱动（清除托盘手动覆盖）
    LogMsg(L"float: ApplyConfig policy=%d (override cleared to policy-driven)", (int)g_app.cfg.policy);
    OnTimer(); // 立即按新策略显隐
    if (visible_) {
        placed_ = false;
        Draw(); // 尺寸 / 描边等外观变化立即生效
    }
}

void FloatingWindow::Show(bool show) {
    override_ = show ? 1 : 0; // 手动覆盖，优先于策略
    if (show) {
        visible_ = true;
        placed_ = false;
        Draw();
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        OnTimer(); // 立即定位到光标旁
    } else {
        visible_ = false;
        ShowWindow(hwnd_, SW_HIDE);
    }
}

} // namespace imeind
