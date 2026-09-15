#include "sand.h"

#include <gdiplus.h>
#include <cmath>
#include <cstdlib>

using namespace Gdiplus;

namespace imeind {

namespace {

// 画布：锚点在顶部中央（对准圆心），沙粒主要散落在圆圈下方
// 宽度要容下惯性拖尾：光标快速横移时旧沙粒会滞留在画布另一侧
constexpr int kCanvasW = 220;
constexpr int kCanvasH = 340;
constexpr float kAnchorX = kCanvasW / 2.0f;
constexpr float kAnchorY = 8.0f;

constexpr int kMaxGrains = 260;
constexpr float kGravity = 130.0f;   // px/s^2：比真实沙子轻，营造“缓缓漏落”感
constexpr float kAirDrag = 2.5f;     // 世界系水平阻力：沙粒离手后逐渐停回原地
constexpr float kTiltDrift = 80.0f;  // 倾斜漂速基准（px/s）：实际取 sin(倾角)×此值，45°≈57

// 彩色沙粒：高饱和糖果色系，每粒在基色上小幅抖动避免死板
const unsigned char kSandPalette[][3] = {
    {255, 110, 90},  // 红
    {255, 200, 80},  // 金
    {120, 220, 160}, // 青绿
    {120, 170, 255}, // 蓝
    {230, 130, 230}, // 粉紫
    {255, 160, 110}, // 橙
    {240, 240, 250}, // 白
};
constexpr int kSandPaletteCount = (int)(sizeof(kSandPalette) / sizeof(kSandPalette[0]));

unsigned char Jitter(unsigned char v, int amt) {
    int j = (int)v + rand() % (2 * amt + 1) - amt;
    if (j < 0) j = 0;
    if (j > 255) j = 255;
    return (unsigned char)j;
}

// 当前 IME 状态对应的沙粒配置（优先级：大写 > 中文 > 英文）
const FxStateCfg& SandState() {
    if (g_app.state.capsLock) return g_app.cfg.sand.caps;
    return g_app.state.chinese ? g_app.cfg.sand.zh : g_app.cfg.sand.en;
}

} // namespace

bool SandWindow::Create(HINSTANCE hInst, HWND owner, FloatingWindow* follow) {
    return ParticleEffect::Create(hInst, owner, follow, L"ImeIndicatorSand",
                                  kCanvasW, kCanvasH, kAnchorX, kAnchorY);
}

bool SandWindow::WantEnabled() const {
    return g_app.cfg.sand.enabled && SandState().mode != 0;
}

void SandWindow::Reset() {
    grains_.clear();
}

// 在锚点附近的小圆内撒出沙粒，初速度主要朝下方散开（指缝漏沙）。
// 沙粒速度存的是世界系速度：生成时继承光标速度（离手瞬间跟手一起动），
// 之后只受重力/空气阻力，画布跟随光标移动 → 旧沙粒自然拖在身后。
void SandWindow::Spawn() {
    Grain g{};
    float a = (rand() % 6283) / 1000.0f;
    float rr = (rand() % 50) / 10.0f; // 0-4.9 px 抓沙范围
    g.x = kAnchorX + cosf(a) * rr;
    g.y = kAnchorY + sinf(a) * rr * 0.6f;
    float ang = 0.15f + (rand() % 2850) / 1000.0f; // 0.15..3.0 rad：下半平面为主
    float speed = 8.0f + rand() % 30;              // 8-37 px/s
    g.vx = cosf(ang) * speed + cursorVx_;
    g.vy = sinf(ang) * speed + cursorVy_;
    // 随机倾斜：每粒在 0~配置角度内随机、左右方向随机，转为恒定横向漂移
    // （不随空气阻力衰减，否则下落轨迹很快又被重力拉回垂直）
    g.drift = 0.0f;
    int tm = g_app.cfg.sand.tiltMax;
    if (tm > 0) {
        float t = (float)(rand() % (tm + 1)) * 3.14159265f / 180.0f;
        if (rand() & 1) t = -t;
        g.drift = sinf(t) * kTiltDrift;
    }
    g.size = 1.2f + (rand() % 11) / 10.0f;         // 半径 1.2-2.2 px，沙粒比烟花粒子细
    g.lifeMs = 1400 + rand() % 1000;
    g.ageMs = 0;
    const FxStateCfg& st = SandState();
    if (st.mode == 2) { // 纯色
        g.r = (unsigned char)((st.color >> 16) & 0xFF);
        g.g = (unsigned char)((st.color >> 8) & 0xFF);
        g.b = (unsigned char)(st.color & 0xFF);
    } else { // 彩色：基色 + 小幅抖动避免死板
        const unsigned char* base = kSandPalette[rand() % kSandPaletteCount];
        g.r = Jitter(base[0], 14);
        g.g = Jitter(base[1], 14);
        g.b = Jitter(base[2], 14);
    }
    grains_.push_back(g);
}

void SandWindow::Step(float dt) {
    if ((int)grains_.size() < kMaxGrains) {
        int n = g_app.cfg.sand.spawnPerTick;
        for (int i = 0; i < n; ++i) Spawn();
    }
    for (size_t i = grains_.size(); i-- > 0;) {
        Grain& g = grains_[i];
        g.vy += kGravity * dt;              // 世界系重力
        g.vx *= (1.0f - kAirDrag * dt);     // 世界系空气阻力：横向越漂越慢
        g.vy *= (1.0f - 0.2f * dt);
        g.x += (g.vx + g.drift - cursorVx_) * dt; // 画布跟手，位移要扣掉光标速度
        g.y += (g.vy - cursorVy_) * dt;
        g.ageMs += (int)(dt * 1000.0f);
        if (g.ageMs >= g.lifeMs || g.y > kCanvasH + 4 || g.x < -4 || g.x > kCanvasW + 4) {
            grains_[i] = grains_.back();
            grains_.pop_back();
        }
    }
}

void SandWindow::Paint(Graphics& g) {
    for (const Grain& gr : grains_) {
        int remain = gr.lifeMs - gr.ageMs;
        int a = 230 * remain / gr.lifeMs;
        if (a < 8) continue;
        SolidBrush br(Color((BYTE)a, gr.r, gr.g, gr.b));
        float d = gr.size * 2.0f;
        g.FillEllipse(&br, gr.x - gr.size, gr.y - gr.size, d, d);
    }
}

} // namespace imeind
