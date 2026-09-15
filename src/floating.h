#pragma once
#ifndef IMIND_FLOATING_H
#define IMIND_FLOATING_H

#include <windows.h>
#include "app.h"

namespace imeind {

// UpdateLayeredWindow 要求预乘 alpha：原地转换 32bpp 顶向下 DIB
// （悬浮窗 / 特效窗基类 / 托盘菜单图标共用）
inline void PremultiplyDib(void* bits, int w, int h) {
    unsigned char* p = (unsigned char*)bits;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            unsigned char b = p[0], g = p[1], r = p[2], a = p[3];
            if (a == 0) {
                p[0] = p[1] = p[2] = 0;
            } else if (a < 255) {
                p[0] = (unsigned char)((b * a + 127) / 255);
                p[1] = (unsigned char)((g * a + 127) / 255);
                p[2] = (unsigned char)((r * a + 127) / 255);
            }
            p += 4;
        }
    }
}

// 悬浮窗：WS_EX_LAYERED + TOPMOST + NOACTIVATE
//  - GDI+ 双缓冲绘制（圆形状态指示：颜色编码 中/英/大写）
//  - 33ms 定时器跟随鼠标（位置 = 光标 + 偏移，越界自动翻转）
//  - 显示策略可配置（始终 / 仅输入时 / 闲置隐藏）
class FloatingWindow {
public:
    bool Create(HINSTANCE hInst, HWND owner);
    void Destroy();
    void UpdateState(const ImeState& s); // 内容变化 → 重绘
    void ApplyConfig();                  // 配置变化 → 穿透样式 / 尺寸
    void Show(bool show);                // 手动显隐（托盘触发）
    bool IsVisible() const { return visible_; }
    HWND Hwnd() const { return hwnd_; }
    // 当前圆圈中心的屏幕坐标（未显示/未定位返回 false）——特效窗跟随用
    bool GetCenter(POINT& p) const {
        if (!visible_ || !placed_ || lastX_ < 0 || width_ <= 0) return false;
        p.x = lastX_ + width_ / 2;
        p.y = lastY_ + height_ / 2;
        return true;
    }

private:
    HWND hwnd_ = nullptr;
    bool visible_ = false;
    int override_ = -1; // 手动显隐覆盖：-1 无干预（遵循策略） / 0 手动隐藏 / 1 手动显示
    int width_ = 0, height_ = 0; // 圆圈尺寸（固定）
    int lastX_ = -1, lastY_ = -1;
    bool placed_ = false;

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    void OnTimer();
    void Draw();
    void ComputeSize(const ImeState& s);
    bool ShouldShow() const;
};

} // namespace imeind

#endif // IMIND_FLOATING_H
