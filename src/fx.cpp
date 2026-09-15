#include "fx.h"

#include <gdiplus.h>
#include <cmath>
#include <cstdlib>

using namespace Gdiplus;

namespace imeind {

namespace {

// 画布：锚点在顶部中央（圆圈上方留白，火箭上升+爆炸都落在可见区内），粒子向下飘落
constexpr int kCanvasW = 220;
constexpr int kCanvasH = 320;
constexpr float kAnchorX = kCanvasW / 2.0f;
constexpr float kAnchorY = 80.0f;

constexpr int kMaxParticles = 400;
constexpr float kGravity = 220.0f;   // px/s^2
constexpr int kSpawnPerTick = 1;     // 中文状态时每帧（33ms）发射的火箭数

// 烟花配色（喜庆系）
const unsigned char kPalette[][3] = {
    {255, 214, 90},  // 金
    {255, 99, 71},   // 红
    {120, 220, 160}, // 青绿
    {120, 170, 255}, // 蓝
    {230, 130, 230}, // 粉
    {255, 255, 255}, // 白
};
constexpr int kPaletteCount = (int)(sizeof(kPalette) / sizeof(kPalette[0]));

// 当前 IME 状态对应的烟花配置（优先级：大写 > 中文 > 英文）
const FxStateCfg& FwState() {
    if (g_app.state.capsLock) return g_app.cfg.fireworks.caps;
    return g_app.state.chinese ? g_app.cfg.fireworks.zh : g_app.cfg.fireworks.en;
}

} // namespace

bool FireworksWindow::Create(HINSTANCE hInst, HWND owner, FloatingWindow* follow) {
    return ParticleEffect::Create(hInst, owner, follow, L"ImeIndicatorFx",
                                  kCanvasW, kCanvasH, kAnchorX, kAnchorY);
}

bool FireworksWindow::WantEnabled() const {
    return g_app.cfg.fireworks.enabled && FwState().mode != 0;
}

void FireworksWindow::Reset() {
    rockets_.clear();
    parts_.clear();
}

// 小火箭：从圆圈处向上发射，短引信后爆炸成粒子（真烟花的节奏感）
void FireworksWindow::Launch() {
    Rocket r{};
    r.x = kAnchorX + (rand() % 24 - 12);
    r.y = kAnchorY;
    float ang = -1.5708f + (rand() % 400 - 200) / 1000.0f; // 近似垂直，±0.2 rad
    float speed = 120.0f + rand() % 60;                    // 120-180 px/s
    r.vx = cosf(ang) * speed;
    r.vy = sinf(ang) * speed;
    r.fuseMs = 220 + rand() % 160;
    rockets_.push_back(r);
}

void FireworksWindow::Explode(float x, float y) {
    const int n = 16 + rand() % 10;
    const FxStateCfg& st = FwState();
    const unsigned char* base = kPalette[rand() % kPaletteCount];
    unsigned char solid[3] = { (unsigned char)((st.color >> 16) & 0xFF),
                               (unsigned char)((st.color >> 8) & 0xFF),
                               (unsigned char)(st.color & 0xFF) };
    if ((int)parts_.size() + n > kMaxParticles) return;
    for (int i = 0; i < n; ++i) {
        Particle p{};
        p.x = x;
        p.y = y;
        float ang = (rand() % 6283) / 1000.0f; // 0..2π
        float speed = 20.0f + rand() % 70;     // 20-90 px/s
        p.vx = cosf(ang) * speed;
        p.vy = sinf(ang) * speed * 0.7f;       // 略扁的爆花形状
        p.size = 2.0f + (rand() % 21) / 10.0f; // 半径 2.0-4.0 px
        p.lifeMs = 700 + rand() % 700;
        p.ageMs = 0;
        // 纯色模式统一色；彩色模式：主体色 + 少量金色点缀，避免每颗完全同色显得死板
        if (st.mode == 2) {
            p.r = solid[0]; p.g = solid[1]; p.b = solid[2];
        } else if (rand() % 5 == 0) {
            p.r = 255; p.g = 235; p.b = 150;
        } else {
            p.r = base[0]; p.g = base[1]; p.b = base[2];
        }
        parts_.push_back(p);
    }
}

void FireworksWindow::Step(float dt) {
    if ((int)parts_.size() < kMaxParticles) {
        for (int i = 0; i < kSpawnPerTick; ++i) Launch();
    }
    // 火箭：上升 + 引信倒数
    for (size_t i = rockets_.size(); i-- > 0;) {
        Rocket& r = rockets_[i];
        r.x += r.vx * dt;
        r.y += r.vy * dt;
        r.vy += 60.0f * dt; // 上升中减速
        r.fuseMs -= (int)(dt * 1000.0f);
        if (r.fuseMs <= 0) {
            Explode(r.x, r.y);
            rockets_[i] = rockets_.back();
            rockets_.pop_back();
        }
    }
    // 粒子：重力下落 + 淡出
    for (size_t i = parts_.size(); i-- > 0;) {
        Particle& p = parts_[i];
        p.vy += kGravity * dt;
        p.vx *= (1.0f - 0.6f * dt); // 空气阻尼
        p.vy *= (1.0f - 0.3f * dt);
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        p.ageMs += (int)(dt * 1000.0f);
        if (p.ageMs >= p.lifeMs || p.y > kCanvasH + 4) {
            parts_[i] = parts_.back();
            parts_.pop_back();
        }
    }
}

void FireworksWindow::Paint(Graphics& g) {
    // 火箭头：明亮小点
    for (const Rocket& r : rockets_) {
        SolidBrush br(Color(255, 255, 244, 200));
        g.FillEllipse(&br, r.x - 1.5f, r.y - 1.5f, 3.0f, 3.0f);
    }
    // 粒子：按寿命淡出
    for (const Particle& p : parts_) {
        int remain = p.lifeMs - p.ageMs;
        int a = 255 * remain / p.lifeMs;
        if (a < 8) continue;
        SolidBrush br(Color((BYTE)a, p.r, p.g, p.b));
        float d = p.size * 2.0f;
        g.FillEllipse(&br, p.x - p.size, p.y - p.size, d, d);
    }
}

} // namespace imeind
