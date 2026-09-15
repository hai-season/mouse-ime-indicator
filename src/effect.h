#pragma once
#ifndef IMIND_EFFECT_H
#define IMIND_EFFECT_H

#include "app.h"

#include <windows.h>

namespace Gdiplus { class Graphics; }

namespace imeind {

class FloatingWindow;

// 粒子特效窗基类：封装「穿透分层窗 + 跟随圆圈锚点 + 不满足条件自动隐藏 +
// 预乘 alpha DIB 绘制 + UpdateLayeredWindow」这套公共机制。
// 新特效只需继承并实现 WantEnabled/Step/Paint 三个钩子，各自独立一个窗口，
// 与悬浮窗及彼此完全解耦。
class ParticleEffect {
public:
    virtual ~ParticleEffect() = default;

    bool Create(HINSTANCE hInst, HWND owner, FloatingWindow* follow,
                const wchar_t* className, int canvasW, int canvasH,
                float anchorX, float anchorY);
    void Destroy();
    HWND Hwnd() const { return hwnd_; }

protected:
    // 该特效本帧是否要显示（开关 + 状态条件；圆圈定位由基类负责）
    virtual bool WantEnabled() const = 0;
    virtual void Step(float dt) = 0;               // 粒子模拟
    virtual void Paint(Gdiplus::Graphics& g) = 0;  // 绘制粒子（画布左上角为原点）
    virtual void Reset() {}                        // 特效隐藏时清空粒子
    virtual float AlphaScale() const { return 1.0f; } // 相对圆圈透明度的亮度系数

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

    HWND hwnd_ = nullptr;
    FloatingWindow* follow_ = nullptr;
    int canvasW_ = 0, canvasH_ = 0;
    float anchorX_ = 0, anchorY_ = 0;
    float cursorVx_ = 0, cursorVy_ = 0; // 光标瞬时速度 px/s（平滑+钳位），子类做惯性用

private:
    void OnTimer();
    void Render();

    bool visible_ = false;
    int lastX_ = -1, lastY_ = -1;
    DWORD lastTick_ = 0;
};

} // namespace imeind

#endif // IMIND_EFFECT_H
