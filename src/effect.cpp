#include "effect.h"
#include "floating.h" // 跟随圆圈位置：FloatingWindow::GetCenter

#include <gdiplus.h>
#include "log.h"

using namespace Gdiplus;

namespace imeind {

bool ParticleEffect::Create(HINSTANCE hInst, HWND owner, FloatingWindow* follow,
                            const wchar_t* className, int canvasW, int canvasH,
                            float anchorX, float anchorY) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = className;
    RegisterClassExW(&wc);

    canvasW_ = canvasW;
    canvasH_ = canvasH;
    anchorX_ = anchorX;
    anchorY_ = anchorY;
    follow_ = follow;

    // 与悬浮窗同款穿透样式：特效层绝不能拦截鼠标
    DWORD ex = WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT;
    hwnd_ = CreateWindowExW(ex, className, L"", WS_POPUP,
                            0, 0, canvasW, canvasH, owner, nullptr, hInst, this);
    if (!hwnd_) {
        LogMsg(L"fx: CreateWindowEx FAILED (%ls) err=%lu", className, GetLastError());
        return false;
    }
    ::SetTimer(hwnd_, 1, 33, nullptr);
    LogMsg(L"fx: window created (%ls) hwnd=%p", className, hwnd_);
    return true;
}

void ParticleEffect::Destroy() {
    if (hwnd_) {
        KillTimer(hwnd_, 1);
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

LRESULT CALLBACK ParticleEffect::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    ParticleEffect* self = nullptr;
    if (msg == WM_NCCREATE) {
        self = reinterpret_cast<ParticleEffect*>(((CREATESTRUCTW*)lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = reinterpret_cast<ParticleEffect*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    switch (msg) {
    case WM_TIMER:
        if (self) self->OnTimer();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_NCHITTEST:
        return HTTRANSPARENT; // 点击穿透
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void ParticleEffect::OnTimer() {
    // 圆圈必须可见且已定位，特效才有锚点
    POINT center{};
    bool want = WantEnabled() && follow_ && follow_->GetCenter(center);

    if (!want) {
        if (visible_) {
            visible_ = false;
            Reset();
            ShowWindow(hwnd_, SW_HIDE);
        }
        return; // 关闭状态：零绘制开销
    }
    if (!visible_) {
        visible_ = true;
        lastX_ = lastY_ = -1; // 首帧只记录位置：别拿隐藏前的旧位置算速度
        cursorVx_ = cursorVy_ = 0.0f;
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    }

    DWORD now = GetTickCount();
    float dt = lastTick_ ? (float)(now - lastTick_) / 1000.0f : 0.033f;
    if (dt > 0.1f) dt = 0.1f; // 系统卡顿时限制步长，防穿透
    lastTick_ = now;

    // 跟随圆圈移动画布（锚点对准圆心）
    int x = (int)(center.x - anchorX_);
    int y = (int)(center.y - anchorY_);

    // 光标瞬时速度：画布位移=光标位移，同一帧的差值即速度；低通平滑去抖
    if (lastX_ >= 0 && dt > 0.001f) {
        float ivx = (float)(x - lastX_) / dt;
        float ivy = (float)(y - lastY_) / dt;
        if (ivx > 3000) ivx = 3000;
        if (ivx < -3000) ivx = -3000;
        if (ivy > 3000) ivy = 3000;
        if (ivy < -3000) ivy = -3000;
        cursorVx_ += (ivx - cursorVx_) * 0.5f;
        cursorVy_ += (ivy - cursorVy_) * 0.5f;
    }

    if (x != lastX_ || y != lastY_) {
        lastX_ = x;
        lastY_ = y;
        SetWindowPos(hwnd_, HWND_TOPMOST, x, y, 0, 0,
                     SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }

    Step(dt);
    Render();
}

void ParticleEffect::Render() {
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = canvasW_;
    bi.bmiHeader.biHeight = -canvasH_;
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
        Paint(g);
    }

    PremultiplyDib(bits, canvasW_, canvasH_);

    POINT src{0, 0};
    SIZE sz{canvasW_, canvasH_};
    // 特效透明度与圆圈一致（AlphaScale 可再单独压暗）
    BYTE alpha = (BYTE)(g_app.cfg.circle.opacity * 255 / 100 * AlphaScale());
    BLENDFUNCTION bf{AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA};
    // pptDst 传 NULL：只更新内容，不移动窗口（位置由 OnTimer 的 SetWindowPos 管）
    UpdateLayeredWindow(hwnd_, nullptr, nullptr, &sz, memdc, &src, 0, &bf, ULW_ALPHA);

    SelectObject(memdc, oldBmp);
    DeleteDC(memdc);
    DeleteObject(hbmp);
}

} // namespace imeind
