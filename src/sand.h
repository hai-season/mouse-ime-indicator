#pragma once
#ifndef IMIND_SAND_H
#define IMIND_SAND_H

#include "effect.h"

#include <vector>

namespace imeind {

// 沙粒特效：中文状态时从圆圈处持续“漏沙”，彩色颗粒向下方散开缓缓掉落。
// 公共机制（穿透窗/跟随/隐藏/DIB 渲染）在 ParticleEffect 基类。
class SandWindow : public ParticleEffect {
public:
    bool Create(HINSTANCE hInst, HWND owner, FloatingWindow* follow);

protected:
    bool WantEnabled() const override;
    void Step(float dt) override;
    void Paint(Gdiplus::Graphics& g) override;
    void Reset() override;

private:
    void Spawn();

    struct Grain {
        float x, y, vx, vy, size;
        float drift; // 倾斜产生的恒定横向漂移速度（px/s，带符号），不受空气阻力影响
        int ageMs, lifeMs;
        unsigned char r, g, b;
    };

    std::vector<Grain> grains_;
};

} // namespace imeind

#endif // IMIND_SAND_H
