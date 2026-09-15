#pragma once
#ifndef IMIND_FX_H
#define IMIND_FX_H

#include "effect.h"

#include <vector>

namespace imeind {

// 烟花特效：中文状态时圆圈持续发射上升小火箭，到期爆炸成彩色粒子下落。
// 公共机制（穿透窗/跟随/隐藏/DIB 渲染）在 ParticleEffect 基类。
class FireworksWindow : public ParticleEffect {
public:
    bool Create(HINSTANCE hInst, HWND owner, FloatingWindow* follow);

protected:
    bool WantEnabled() const override;
    void Step(float dt) override;
    void Paint(Gdiplus::Graphics& g) override;
    void Reset() override;

private:
    void Launch();
    void Explode(float x, float y);

    struct Rocket {
        float x, y, vx, vy;
        int fuseMs;
    };
    struct Particle {
        float x, y, vx, vy, size;
        int ageMs, lifeMs;
        unsigned char r, g, b;
    };

    std::vector<Rocket> rockets_;
    std::vector<Particle> parts_;
};

} // namespace imeind

#endif // IMIND_FX_H
