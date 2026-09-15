#include "settings.h"

#include "app.h"
#include "config.h"

#include <windows.h>
#include <gdiplus.h>
#include <cstdlib>
#include <cwchar>

namespace imeind {

// MinGW-w64 头文件缺失常量（MS SDK 值：WM_USER + 3）
#ifndef EM_SETBKGNDCOLOR
#define EM_SETBKGNDCOLOR (WM_USER + 3)
#endif

// 颜色选择器：不依赖 commdlg.h 的宏开关，直接按 MS SDK 布局自声明 CHOOSECOLORW
struct CtxChooseColorW {
    DWORD lStructSize;
    HWND hwndOwner;
    void* hInstance;
    COLORREF rgbResult;
    COLORREF* lpCustColors;
    DWORD Flags;
    LPARAM lCustData;
    void* lpfnHook;
    const wchar_t* lpTemplateName;
};
extern "C" WINBOOL WINAPI ChooseColorW(CtxChooseColorW*);
constexpr DWORD kCCRGBINIT = 0x00000001;
constexpr DWORD kCCSOLIDCOLOR = 0x00000080;
constexpr DWORD kCCANYCOLOR = 0x00000200;

namespace {

constexpr int ID_RADIO_ALWAYS  = 101;
constexpr int ID_RADIO_FOCUS   = 102;
constexpr int ID_RADIO_IDLE    = 103;
constexpr int ID_EDIT_IDLE     = 104;
constexpr int ID_EDIT_OFFX     = 105;
constexpr int ID_EDIT_OFFY     = 106;
constexpr int ID_CHK_AUTOSTART = 108;
constexpr int ID_LBL_IDLE      = 109;
constexpr int ID_BTN_OK        = 110;
constexpr int ID_BTN_CANCEL    = 111;
constexpr int ID_EDIT_SIZE     = 112; // 圆圈直径
constexpr int ID_EDIT_BORDER   = 113; // 描边宽度
constexpr int ID_EDIT_OPACITY  = 114; // 不透明度 %
constexpr int ID_LBL_OPACITY   = 221; // 字段名：透明度
constexpr int ID_LBL_PCT       = 222; // 单位 %
constexpr int ID_LBL_COLOR     = 205; // 字段名：状态颜色
constexpr int ID_BTN_COL_ZH    = 130; // 色块：中文
constexpr int ID_BTN_COL_YING  = 131; // 色块：英
constexpr int ID_BTN_COL_EN    = 132; // 色块：EN
constexpr int ID_BTN_COL_CAPS  = 133; // 色块：大写
constexpr int ID_CHK_FIREWORKS = 134; // 复选框：启用烟花
constexpr int ID_CHK_SAND      = 135; // 复选框：启用沙粒
constexpr int ID_CHK_CIRCLE    = 136; // 复选框：启用圆圈
constexpr int ID_EDIT_SPAWN    = 117; // 输入框：每帧沙粒生成数
constexpr int ID_LBL_SPAWN     = 232; // 字段名：生成数量
constexpr int ID_LBL_SPAWN_UNIT= 233; // 单位 粒/帧
constexpr int ID_EDIT_TILT     = 118; // 输入框：倾斜角度上限
constexpr int ID_LBL_TILT      = 234; // 字段名：倾斜角度
constexpr int ID_LBL_TILT_UNIT = 235; // 单位 度
// 烟花/沙粒三态控件：每态一个「模式循环按钮（不显示/彩色/纯色）」+ 一个纯色色块
constexpr int ID_BTN_FW_ZH = 140, ID_BTN_FW_EN = 141, ID_BTN_FW_CAPS = 142;
constexpr int ID_CHIP_FW_ZH = 145, ID_CHIP_FW_EN = 146, ID_CHIP_FW_CAPS = 147;
constexpr int ID_LBL_FW_ZH = 240, ID_LBL_FW_EN = 241, ID_LBL_FW_CAPS = 242;
constexpr int ID_BTN_SD_ZH = 150, ID_BTN_SD_EN = 151, ID_BTN_SD_CAPS = 152;
constexpr int ID_CHIP_SD_ZH = 155, ID_CHIP_SD_EN = 156, ID_CHIP_SD_CAPS = 157;
constexpr int ID_LBL_SD_ZH = 243, ID_LBL_SD_EN = 244, ID_LBL_SD_CAPS = 245;
constexpr int ID_HDR_FX        = 224; // 分区标题：特效
constexpr int ID_SEP_FX        = 225; // 分隔线
constexpr int ID_HDR_CIRCLE    = 226; // 子标题：圆圈特效
constexpr int ID_HDR_FIRE      = 228; // 子标题：烟花特效
constexpr int ID_HDR_SAND      = 229; // 子标题：沙粒特效
constexpr int ID_HDR_MISC      = 230; // 分区标题：其他
constexpr int ID_SEP_MISC      = 231; // 分隔线
constexpr int ID_HDR_POLICY    = 201; // 分区标题：显示策略
constexpr int ID_HDR_OFFSET    = 202; // 分区标题：位置偏移
constexpr int ID_SEP_POLICY    = 211; // 分隔线
constexpr int ID_SEP_OFFSET    = 212;
constexpr int ID_LBL_X         = 214;
constexpr int ID_LBL_Y         = 215;
constexpr int ID_LBL_PX1       = 216;
constexpr int ID_LBL_PX2       = 217;
constexpr int ID_LBL_HINT      = 218;
constexpr int ID_LBL_SIZE      = 219; // 字段名：圆圈大小
constexpr int ID_LBL_BORDER    = 220; // 字段名：描边宽度

HFONT g_font = nullptr;       // 主控件字体
HFONT g_fontHeader = nullptr; // 分区标题字体
HWND g_hSettingsDlg = nullptr; // 当前打开的设置对话框（重入守卫）

// DPI 缩放：布局坐标全部按 96dpi 逻辑像素书写，创建/重排时乘以 g_uiScale
// （进程是 PerMonitorV2 感知，物理像素建窗口——不缩放的话在高缩放屏上整窗只有"100%大小"）
float g_uiScale = 1.0f;
int S(int v) { return (int)(v * g_uiScale + 0.5f); }
float SF(float v) { return v * g_uiScale; }

// 深色模式配色（参照 VS Code Dark+：背景 #1E1E1E / 输入框 #3C3C3C / 文字 #D4D4D4 / 强调 #007ACC）
const COLORREF kDlgBg = RGB(30, 30, 30);
const COLORREF kEditBg = RGB(60, 60, 60);
const COLORREF kTextBg = RGB(212, 212, 212);
const COLORREF kTextDim = RGB(157, 157, 157);
const COLORREF kHeaderText = RGB(79, 193, 255);
HBRUSH g_dlgBrush = nullptr;
HBRUSH g_editBrush = nullptr;

const Gdiplus::Color kAccent(255, 0, 122, 204);      // #007ACC
const Gdiplus::Color kAccentHover(255, 31, 136, 210);
const Gdiplus::Color kAccentDown(255, 0, 102, 170);
const Gdiplus::Color kText(255, 212, 212, 212);      // #D4D4D4
const Gdiplus::Color kTextHover(255, 255, 255, 255);
const Gdiplus::Color kOutline(255, 138, 138, 138);   // 未选中描边
const Gdiplus::Color kCtrlBg(255, 30, 30, 30);       // 对话框底色
const Gdiplus::Color kBtnSecBg(255, 60, 60, 60);     // 次按钮
const Gdiplus::Color kBtnSecHover(255, 74, 74, 74);
const Gdiplus::Color kBtnSecDown(255, 84, 84, 84);
const Gdiplus::Color kBtnSecBorder(255, 80, 80, 80);
const Gdiplus::Color kSepColor(255, 63, 63, 70);     // 分隔线

// 状态色待提交值：0=中文 1=英 2=EN 3=大写（0xRRGGBB；打开时从 cfg 初始化，确定时写回）
int g_colPending[4] = {0};
COLORREF g_custColors[16] = {0}; // ChooseColor 自定义色槽（须存活于对话框期间）

// 特效三态待提交值：[0]=烟花 [1]=沙粒；[0]=中文 [1]=英文 [2]=大写
int g_fxMode[2][3] = {{1, 0, 0}, {1, 0, 0}};
int g_fxColor[2][3] = {{0xFFD24A, 0x4FC3F7, 0xFF5252}, {0xD8B26A, 0x4FC3F7, 0xFF5252}};

// id → (特效, 状态) 反查
bool FxBtnMap(int id, int& eff, int& idx) {
    if (id >= ID_BTN_FW_ZH && id <= ID_BTN_FW_CAPS) { eff = 0; idx = id - ID_BTN_FW_ZH; return true; }
    if (id >= ID_BTN_SD_ZH && id <= ID_BTN_SD_CAPS) { eff = 1; idx = id - ID_BTN_SD_ZH; return true; }
    return false;
}
bool FxChipMap(int id, int& eff, int& idx) {
    if (id >= ID_CHIP_FW_ZH && id <= ID_CHIP_FW_CAPS) { eff = 0; idx = id - ID_CHIP_FW_ZH; return true; }
    if (id >= ID_CHIP_SD_ZH && id <= ID_CHIP_SD_CAPS) { eff = 1; idx = id - ID_CHIP_SD_ZH; return true; }
    return false;
}
const wchar_t* FxModeText(int m) { return m == 1 ? L"彩色" : m == 2 ? L"纯色" : L"不显示"; }
void RefreshFxModeBtn(HWND dlg, int eff, int idx) {
    int id = (eff == 0 ? ID_BTN_FW_ZH : ID_BTN_SD_ZH) + idx;
    SetWindowTextW(GetDlgItem(dlg, id), FxModeText(g_fxMode[eff][idx]));
    InvalidateRect(GetDlgItem(dlg, id), nullptr, TRUE);
}

// owner-draw 按钮的悬停/选中态（存在窗口属性里：GWLP_USERDATA 可能被按钮内部占用）
// 纯 BS_OWNERDRAW 不持久化 BM_SETCHECK（写 BM_GETCHECK 恒读 0），选中态只能自管
struct BtnState { bool hover; bool checked; };
const wchar_t* kBtnStateProp = L"ImeBtnState";
const wchar_t* kBtnOrigProp = L"ImeBtnOrigProc";

void SetBtnChecked(HWND hb, bool on) {
    BtnState* st = (BtnState*)GetPropW(hb, kBtnStateProp);
    if (st && st->checked != on) {
        st->checked = on;
        InvalidateRect(hb, nullptr, TRUE);
    }
}
bool GetBtnChecked(HWND hb) {
    BtnState* st = (BtnState*)GetPropW(hb, kBtnStateProp);
    return st && st->checked;
}
bool IsBtnChecked(HWND dlg, int id) { return GetBtnChecked(GetDlgItem(dlg, id)); }

void RoundRectPath(Gdiplus::GraphicsPath& p, float x, float y, float w, float h, float r) {
    float d = r * 2.0f;
    p.AddArc(x, y, d, d, 180, 90);
    p.AddArc(x + w - d, y, d, d, 270, 90);
    p.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    p.AddArc(x, y + h - d, d, d, 90, 90);
    p.CloseFigure();
}

void DrawControlText(DRAWITEMSTRUCT* dis, Gdiplus::Color color, int leftPad) {
    wchar_t text[128] = {0};
    GetWindowTextW(dis->hwndItem, text, 128);
    if (!text[0]) return;
    HDC hdc = dis->hDC;
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(color.GetR(), color.GetG(), color.GetB()));
    HFONT old = (HFONT)SelectObject(hdc, g_font);
    RECT tr = dis->rcItem;
    tr.left += leftPad;
    DrawTextW(hdc, text, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(hdc, old);
}

// 自绘单选：空心圈 / 选中为强调色圈 + 实心点
void DrawRadioButton(DRAWITEMSTRUCT* dis, bool checked) {
    RECT rc = dis->rcItem;
    FillRect(dis->hDC, &rc, g_dlgBrush);
    BtnState* st = (BtnState*)GetPropW(dis->hwndItem, kBtnStateProp);
    Gdiplus::Graphics g(dis->hDC);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    float cx = rc.left + SF(9);
    float cy = (rc.top + rc.bottom) / 2.0f;
    Gdiplus::Pen ring(checked ? kAccent : kOutline, SF(1.4f));
    g.DrawEllipse(&ring, cx - SF(7), cy - SF(7), SF(14), SF(14));
    if (checked) {
        Gdiplus::SolidBrush dot(kAccent);
        g.FillEllipse(&dot, cx - SF(3.4f), cy - SF(3.4f), SF(6.8f), SF(6.8f));
    }
    DrawControlText(dis, st && st->hover ? kTextHover : kText, S(24));
}

// 自绘复选：圆角方框 + 白色对勾
void DrawCheckBox(DRAWITEMSTRUCT* dis, bool checked) {
    RECT rc = dis->rcItem;
    FillRect(dis->hDC, &rc, g_dlgBrush);
    BtnState* st = (BtnState*)GetPropW(dis->hwndItem, kBtnStateProp);
    float cy = (rc.top + rc.bottom) / 2.0f;
    float bx = (float)rc.left;
    Gdiplus::Graphics g(dis->hDC);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::GraphicsPath box;
    RoundRectPath(box, bx + SF(1), cy - SF(8), SF(16), SF(16), SF(3.5f));
    if (checked) {
        Gdiplus::SolidBrush fill(kAccent);
        g.FillPath(&fill, &box);
        Gdiplus::Pen check(Gdiplus::Color(255, 255, 255, 255), SF(2));
        check.SetStartCap(Gdiplus::LineCapRound);
        check.SetEndCap(Gdiplus::LineCapRound);
        check.SetLineJoin(Gdiplus::LineJoinRound);
        Gdiplus::PointF pts[3] = { Gdiplus::PointF(bx + SF(4.5f), cy),
                                   Gdiplus::PointF(bx + SF(8), cy + SF(3.5f)),
                                   Gdiplus::PointF(bx + SF(13), cy - SF(4)) };
        g.DrawLines(&check, pts, 3);
    } else {
        Gdiplus::Pen outline(kOutline, SF(1.2f));
        g.DrawPath(&outline, &box);
    }
    DrawControlText(dis, st && st->hover ? kTextHover : kText, S(26));
}

// 自绘按钮：主按钮强调蓝，次按钮深灰；悬停/按下有状态色（按下态由 ODS_SELECTED 提供）
void DrawButton(DRAWITEMSTRUCT* dis, bool primary) {
    RECT rc = dis->rcItem;
    FillRect(dis->hDC, &rc, g_dlgBrush);
    BtnState* st = (BtnState*)GetPropW(dis->hwndItem, kBtnStateProp);
    bool down = (dis->itemState & ODS_SELECTED) != 0;
    bool hover = st && st->hover;
    Gdiplus::Color bg;
    if (primary) bg = down ? kAccentDown : (hover ? kAccentHover : kAccent);
    else bg = down ? kBtnSecDown : (hover ? kBtnSecHover : kBtnSecBg);

    Gdiplus::Graphics g(dis->hDC);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::GraphicsPath path;
    RoundRectPath(path, (float)rc.left, (float)rc.top, (float)(rc.right - rc.left),
                  (float)(rc.bottom - rc.top), SF(5));
    Gdiplus::SolidBrush fill(bg);
    g.FillPath(&fill, &path);
    if (!primary) {
        Gdiplus::Pen pen(kBtnSecBorder, 1.0f);
        g.DrawPath(&pen, &path);
    }
    if (dis->itemState & ODS_FOCUS) {
        Gdiplus::Pen focusPen(Gdiplus::Color(160, 255, 255, 255), 1.0f);
        Gdiplus::GraphicsPath inner;
        RoundRectPath(inner, (float)rc.left + SF(1.5f), (float)rc.top + SF(1.5f),
                      (float)(rc.right - rc.left) - SF(3), (float)(rc.bottom - rc.top) - SF(3),
                      SF(4));
        g.DrawPath(&focusPen, &inner);
    }

    wchar_t text[64] = {0};
    GetWindowTextW(dis->hwndItem, text, 64);
    if (!text[0]) return;
    Gdiplus::Font font(dis->hDC, g_font);
    Gdiplus::SolidBrush tb(primary ? Gdiplus::Color(255, 255, 255, 255) : kText);
    Gdiplus::StringFormat sf;
    sf.SetAlignment(Gdiplus::StringAlignmentCenter);
    sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    Gdiplus::RectF tr((Gdiplus::REAL)rc.left, (Gdiplus::REAL)rc.top,
                      (Gdiplus::REAL)(rc.right - rc.left), (Gdiplus::REAL)(rc.bottom - rc.top));
    g.DrawString(text, (INT)wcslen(text), &font, tr, &sf, &tb);
}

// 自绘色块：圆角矩形填充当前待提交色 + 状态文字（按亮度选黑/白字保证可读）
void DrawColorSwatch(DRAWITEMSTRUCT* dis, int idx) {
    RECT rc = dis->rcItem;
    FillRect(dis->hDC, &rc, g_dlgBrush);
    int rgb = g_colPending[idx];
    int r = (rgb >> 16) & 0xFF, gv = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    Gdiplus::Graphics g(dis->hDC);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::GraphicsPath path;
    RoundRectPath(path, (float)rc.left + 0.5f, (float)rc.top + 0.5f,
                  (float)(rc.right - rc.left) - 1.0f, (float)(rc.bottom - rc.top) - 1.0f,
                  SF(5));
    Gdiplus::SolidBrush fill(Gdiplus::Color(255, (BYTE)r, (BYTE)gv, (BYTE)b));
    g.FillPath(&fill, &path);
    bool down = (dis->itemState & ODS_SELECTED) != 0;
    Gdiplus::Pen pen(down ? kAccent : kBtnSecBorder, down ? SF(1.5f) : 1.0f);
    g.DrawPath(&pen, &path);

    wchar_t text[32] = {0};
    GetWindowTextW(dis->hwndItem, text, 32);
    if (!text[0]) return;
    int lum = (r * 299 + gv * 587 + b * 114) / 1000;
    Gdiplus::Font font(dis->hDC, g_font);
    Gdiplus::SolidBrush tb(lum >= 150 ? Gdiplus::Color(255, 20, 20, 20)
                                      : Gdiplus::Color(255, 240, 240, 240));
    Gdiplus::StringFormat sf;
    sf.SetAlignment(Gdiplus::StringAlignmentCenter);
    sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    Gdiplus::RectF tr((Gdiplus::REAL)rc.left, (Gdiplus::REAL)rc.top,
                      (Gdiplus::REAL)(rc.right - rc.left), (Gdiplus::REAL)(rc.bottom - rc.top));
    g.DrawString(text, (INT)wcslen(text), &font, tr, &sf, &tb);
}

// 自绘纯色色块（无文字）：点开颜色选择器，选色后该状态自动切为纯色
void DrawColorChip(DRAWITEMSTRUCT* dis, int rgb) {
    FillRect(dis->hDC, &dis->rcItem, g_dlgBrush);
    RECT rc = dis->rcItem;
    bool down = (dis->itemState & ODS_SELECTED) != 0;
    Gdiplus::Graphics g(dis->hDC);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::GraphicsPath path;
    RoundRectPath(path, (float)rc.left + 1.0f, (float)rc.top + 2.0f,
                  (float)(rc.right - rc.left) - 2.0f, (float)(rc.bottom - rc.top) - 4.0f, SF(4));
    Gdiplus::SolidBrush fill(Gdiplus::Color(255, (BYTE)((rgb >> 16) & 0xFF),
                                           (BYTE)((rgb >> 8) & 0xFF), (BYTE)(rgb & 0xFF)));
    g.FillPath(&fill, &path);
    Gdiplus::Pen pen(down ? kAccent : kBtnSecBorder, down ? SF(1.5f) : 1.0f);
    g.DrawPath(&pen, &path);
}

// 自绘分隔线
void DrawSeparator(DRAWITEMSTRUCT* dis) {
    RECT rc = dis->rcItem;
    FillRect(dis->hDC, &rc, g_dlgBrush);
    int y = (rc.top + rc.bottom) / 2;
    HBRUSH br = CreateSolidBrush(RGB(63, 63, 70));
    RECT line{rc.left, y, rc.right, y + 1};
    FillRect(dis->hDC, &line, br);
    DeleteObject(br);
}

// 自绘静态文字：标题亮蓝 / 字段名浅色 / 单位与提示次要灰
void DrawStaticText(DRAWITEMSTRUCT* dis) {
    RECT rc = dis->rcItem;
    FillRect(dis->hDC, &rc, g_dlgBrush);
    bool header = (dis->CtlID == ID_HDR_POLICY || dis->CtlID == ID_HDR_OFFSET ||
                   dis->CtlID == ID_HDR_FX || dis->CtlID == ID_HDR_CIRCLE ||
                   dis->CtlID == ID_HDR_FIRE || dis->CtlID == ID_HDR_SAND ||
                   dis->CtlID == ID_HDR_MISC);
    bool fieldName = (dis->CtlID == ID_LBL_SIZE || dis->CtlID == ID_LBL_BORDER ||
                      dis->CtlID == ID_LBL_OPACITY || dis->CtlID == ID_LBL_COLOR ||
                      dis->CtlID == ID_LBL_SPAWN || dis->CtlID == ID_LBL_TILT ||
                      (dis->CtlID >= ID_LBL_FW_ZH && dis->CtlID <= ID_LBL_SD_CAPS));
    COLORREF color = header ? kHeaderText : (fieldName ? kTextBg : kTextDim);
    wchar_t text[128] = {0};
    GetWindowTextW(dis->hwndItem, text, 128);
    if (!text[0]) return;
    HDC hdc = dis->hDC;
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, color);
    HFONT hf = header ? g_fontHeader : g_font;
    if (!hf) hf = g_font;
    HFONT old = (HFONT)SelectObject(hdc, hf);
    RECT tr = rc;
    DrawTextW(hdc, text, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(hdc, old);
}

LRESULT CALLBACK BtnSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    WNDPROC orig = (WNDPROC)GetPropW(hwnd, kBtnOrigProp);
    BtnState* st = (BtnState*)GetPropW(hwnd, kBtnStateProp);
    if (msg == WM_MOUSEMOVE) {
        if (st && !st->hover) {
            st->hover = true;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&tme);
    } else if (msg == WM_MOUSELEAVE) {
        if (st && st->hover) {
            st->hover = false;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
    } else if (msg == WM_NCDESTROY) {
        if (st) delete st; // 属性在窗口销毁时由系统自动清理
    }
    return CallWindowProcW(orig, hwnd, msg, wp, lp);
}

// 输入框子类化：圆角区域 + 自绘 1px 边框（常规 #464646 / 禁用更暗 / 聚焦 #007ACC）
LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    WNDPROC orig = (WNDPROC)GetPropW(hwnd, L"ImeEditOrig");
    if (msg == WM_SETFOCUS || msg == WM_KILLFOCUS) {
        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE);
    } else if (msg == WM_PAINT) {
        CallWindowProcW(orig, hwnd, msg, wp, lp);
        HDC hdc = GetDC(hwnd);
        RECT rc;
        GetClientRect(hwnd, &rc);
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        bool focus = (GetFocus() == hwnd);
        bool enabled = IsWindowEnabled(hwnd) != FALSE;
        Gdiplus::Color bc = focus ? kAccent
                                  : (enabled ? Gdiplus::Color(255, 70, 70, 78)
                                             : Gdiplus::Color(255, 50, 50, 56));
        Gdiplus::Pen pen(bc, 1.0f);
        Gdiplus::GraphicsPath path;
        RoundRectPath(path, 0.5f, 0.5f, (float)rc.right - 1.0f, (float)rc.bottom - 1.0f,
                      SF(3.5f));
        g.DrawPath(&pen, &path);
        ReleaseDC(hwnd, hdc);
        return 0;
    }
    return CallWindowProcW(orig, hwnd, msg, wp, lp);
}

// Win11 (>= 22000) 用 Segoe UI Variable，旧系统回退 Segoe UI
HFONT MakeFont(int px, int weight) {
    OSVERSIONINFOW ovi{};
    ovi.dwOSVersionInfoSize = sizeof(ovi);
    bool win11 = false;
    HMODULE hNt = GetModuleHandleW(L"ntdll.dll");
    if (hNt) {
        typedef LONG(WINAPI* RtlGetVersionFn)(OSVERSIONINFOW*);
        RtlGetVersionFn fn = (RtlGetVersionFn)GetProcAddress(hNt, "RtlGetVersion");
        if (fn && fn(&ovi) == 0)
            win11 = ovi.dwMajorVersion == 10 && ovi.dwBuildNumber >= 22000;
    }
    return CreateFontW(-px, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE,
                       win11 ? L"Segoe UI Variable" : L"Segoe UI");
}

HWND CreateCtrl(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style,
                int x, int y, int w, int h, int id) {
    if (wcscmp(cls, L"STATIC") == 0) style |= SS_OWNERDRAW;
    HWND hw = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                              x, y, w, h, parent, (HMENU)(INT_PTR)id, g_app.hInst, nullptr);
    if (!hw) return nullptr;
    if (g_font) SendMessageW(hw, WM_SETFONT, (WPARAM)g_font, TRUE);
    if (wcscmp(cls, L"EDIT") == 0) {
        SendMessageW(hw, EM_SETBKGNDCOLOR, 0, kEditBg);
        SetPropW(hw, L"ImeEditOrig",
                 (HANDLE)GetWindowLongPtrW(hw, GWLP_WNDPROC));
        SetWindowLongPtrW(hw, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);
        SetWindowRgn(hw, CreateRoundRectRgn(0, 0, w + 1, h + 1, S(8), S(8)), TRUE);
    } else if (wcscmp(cls, L"BUTTON") == 0) {
        // BS_OWNERDRAW 即可（按钮的 radio/check 样式位与之互斥，选中态存 BtnState 手动管理）
        BtnState* st = new BtnState{false, false};
        SetPropW(hw, kBtnStateProp, st);
        SetPropW(hw, kBtnOrigProp, (HANDLE)GetWindowLongPtrW(hw, GWLP_WNDPROC));
        SetWindowLongPtrW(hw, GWLP_WNDPROC, (LONG_PTR)BtnSubclassProc);
    }
    return hw;
}

// 控件规格表：坐标一律 96dpi 逻辑像素，创建/重排时乘 g_uiScale
enum CtrlKind { CK_STATIC, CK_HEADER, CK_SEPARATOR, CK_EDIT, CK_BUTTON };
struct CtrlSpec {
    CtrlKind kind;
    int id;
    const wchar_t* text;
    DWORD style;
    int x, y, w, h;
};
// 布局：显示策略 → 位置偏移 → 特效（圆圈/烟花/沙粒各一个子区块，新增特效往下排）→ 其他
const CtrlSpec kSpecs[] = {
    {CK_HEADER,    ID_HDR_POLICY,  L"显示策略",             0,                       20, 14,  200, 18},
    {CK_SEPARATOR, ID_SEP_POLICY,  L"",                    0,                       20, 36,  360,  2},
    {CK_BUTTON,    ID_RADIO_ALWAYS,L"始终显示",              BS_OWNERDRAW | WS_GROUP, 20, 46,  140, 24},
    {CK_BUTTON,    ID_RADIO_FOCUS, L"仅输入框聚焦时显示",      BS_OWNERDRAW,            20, 76,  200, 24},
    {CK_BUTTON,    ID_RADIO_IDLE,  L"闲置自动隐藏",           BS_OWNERDRAW,            20, 106, 128, 24},
    {CK_EDIT,      ID_EDIT_IDLE,   L"",                    ES_NUMBER,               152, 106,  52, 24},
    {CK_STATIC,    ID_LBL_IDLE,    L"秒",                  SS_LEFT,                 210, 110,  20, 16},
    {CK_HEADER,    ID_HDR_OFFSET,  L"位置偏移（像素）",        0,                       20, 142, 200, 18},
    {CK_SEPARATOR, ID_SEP_OFFSET,  L"",                    0,                       20, 164, 360,  2},
    {CK_STATIC,    ID_LBL_X,       L"X",                   SS_LEFT,                 20, 172,  12, 16},
    {CK_EDIT,      ID_EDIT_OFFX,   L"",                    ES_NUMBER | WS_GROUP,    38, 168,  64, 24},
    {CK_STATIC,    ID_LBL_Y,       L"Y",                   SS_LEFT,                 130, 172,  12, 16},
    {CK_EDIT,      ID_EDIT_OFFY,   L"",                    ES_NUMBER,               148, 168,  64, 24},
    {CK_HEADER,    ID_HDR_FX,      L"特效",                 0,                        20, 204, 200, 18},
    {CK_SEPARATOR, ID_SEP_FX,      L"",                    0,                        20, 226, 360,  2},
    // —— 圆圈特效（悬浮窗本体；禁用时窗口仍作特效锚点跟随光标）——
    {CK_HEADER,    ID_HDR_CIRCLE,  L"圆圈特效",             0,                        20, 234, 120, 18},
    {CK_BUTTON,    ID_CHK_CIRCLE,  L"启用（关闭后仅特效跟随光标，不显示圆圈）", BS_OWNERDRAW | WS_GROUP, 36, 258, 344, 24},
    {CK_STATIC,    ID_LBL_SIZE,    L"圆圈大小",              SS_LEFT,                  36, 288,  60, 20},
    {CK_EDIT,      ID_EDIT_SIZE,   L"",                    ES_NUMBER,                100, 286,  52, 24},
    {CK_STATIC,    ID_LBL_PX1,     L"px",                  SS_LEFT,                  156, 292,  18, 16},
    {CK_STATIC,    ID_LBL_BORDER,  L"描边宽度",              SS_LEFT,                  212, 288,  60, 20},
    {CK_EDIT,      ID_EDIT_BORDER, L"",                    ES_NUMBER,                276, 286,  52, 24},
    {CK_STATIC,    ID_LBL_PX2,     L"px",                  SS_LEFT,                  332, 292,  18, 16},
    {CK_STATIC,    ID_LBL_OPACITY, L"透明度",               SS_LEFT,                  36, 316,  60, 20},
    {CK_EDIT,      ID_EDIT_OPACITY,L"",                    ES_NUMBER,                100, 314,  52, 24},
    {CK_STATIC,    ID_LBL_PCT,     L"%",                   SS_LEFT,                  156, 318,  18, 16},
    {CK_STATIC,    ID_LBL_COLOR,   L"状态颜色",              SS_LEFT,                  36, 344,  80, 20},
    {CK_BUTTON,    ID_BTN_COL_ZH,  L"中文",                BS_OWNERDRAW | WS_GROUP,   36, 366,  78, 32},
    {CK_BUTTON,    ID_BTN_COL_YING,L"英",                  BS_OWNERDRAW,             130, 366,  78, 32},
    {CK_BUTTON,    ID_BTN_COL_EN,  L"EN",                  BS_OWNERDRAW,             224, 366,  78, 32},
    {CK_BUTTON,    ID_BTN_COL_CAPS,L"大写",                BS_OWNERDRAW,             318, 366,  78, 32},
    {CK_STATIC,    ID_LBL_HINT,    L"大小 8-64，描边 0-10（0 = 无），透明度 20-100",
                                                    SS_LEFT,                  36, 406, 344, 16},
    // —— 烟花特效 —— 每态一行：模式按钮（不显示/彩色/纯色 循环）+ 纯色色块
    {CK_HEADER,    ID_HDR_FIRE,    L"烟花特效",             0,                        20, 436, 120, 18},
    {CK_BUTTON,    ID_CHK_FIREWORKS,L"启用",                BS_OWNERDRAW,              36, 460,  80, 24},
    {CK_STATIC,    ID_LBL_FW_ZH,   L"中文",                SS_LEFT,                   36, 490,  26, 16},
    {CK_BUTTON,    ID_BTN_FW_ZH,   L"彩色",                BS_OWNERDRAW | WS_GROUP,   64, 486,  56, 24},
    {CK_BUTTON,    ID_CHIP_FW_ZH,  L"",                    BS_OWNERDRAW,              124, 486, 26, 24},
    {CK_STATIC,    ID_LBL_FW_EN,   L"英文",                SS_LEFT,                  158, 490,  26, 16},
    {CK_BUTTON,    ID_BTN_FW_EN,   L"不显示",              BS_OWNERDRAW,              186, 486, 56, 24},
    {CK_BUTTON,    ID_CHIP_FW_EN,  L"",                    BS_OWNERDRAW,              246, 486, 26, 24},
    {CK_STATIC,    ID_LBL_FW_CAPS, L"大写",                SS_LEFT,                  278, 490,  26, 16},
    {CK_BUTTON,    ID_BTN_FW_CAPS, L"不显示",              BS_OWNERDRAW,              306, 486, 56, 24},
    {CK_BUTTON,    ID_CHIP_FW_CAPS,L"",                    BS_OWNERDRAW,              366, 486, 26, 24},
    // —— 沙粒特效 ——
    {CK_HEADER,    ID_HDR_SAND,    L"沙粒特效",             0,                        20, 526, 120, 18},
    {CK_BUTTON,    ID_CHK_SAND,    L"启用",                BS_OWNERDRAW,              36, 550,  80, 24},
    {CK_STATIC,    ID_LBL_SD_ZH,   L"中文",                SS_LEFT,                   36, 580,  26, 16},
    {CK_BUTTON,    ID_BTN_SD_ZH,   L"彩色",                BS_OWNERDRAW | WS_GROUP,   64, 576,  56, 24},
    {CK_BUTTON,    ID_CHIP_SD_ZH,  L"",                    BS_OWNERDRAW,              124, 576, 26, 24},
    {CK_STATIC,    ID_LBL_SD_EN,   L"英文",                SS_LEFT,                  158, 580,  26, 16},
    {CK_BUTTON,    ID_BTN_SD_EN,   L"不显示",              BS_OWNERDRAW,              186, 576, 56, 24},
    {CK_BUTTON,    ID_CHIP_SD_EN,  L"",                    BS_OWNERDRAW,              246, 576, 26, 24},
    {CK_STATIC,    ID_LBL_SD_CAPS, L"大写",                SS_LEFT,                  278, 580,  26, 16},
    {CK_BUTTON,    ID_BTN_SD_CAPS, L"不显示",              BS_OWNERDRAW,              306, 576, 56, 24},
    {CK_BUTTON,    ID_CHIP_SD_CAPS,L"",                    BS_OWNERDRAW,              366, 576, 26, 24},
    {CK_STATIC,    ID_LBL_SPAWN,   L"生成数量",              SS_LEFT,                  36, 608,  60, 20},
    {CK_EDIT,      ID_EDIT_SPAWN,  L"",                    ES_NUMBER,                100, 606,  52, 24},
    {CK_STATIC,    ID_LBL_SPAWN_UNIT,L"粒/帧（1-8）",         SS_LEFT,                  156, 610,  90, 16},
    {CK_STATIC,    ID_LBL_TILT,    L"倾斜角度",              SS_LEFT,                  36, 636,  60, 20},
    {CK_EDIT,      ID_EDIT_TILT,   L"",                    ES_NUMBER,                100, 634,  52, 24},
    {CK_STATIC,    ID_LBL_TILT_UNIT,L"度（左右随机 0-45）",    SS_LEFT,                  156, 638,  150, 16},
    // —— 其他 ——
    {CK_HEADER,    ID_HDR_MISC,    L"其他",                 0,                        20, 680, 200, 18},
    {CK_SEPARATOR, ID_SEP_MISC,    L"",                    0,                        20, 702, 360,  2},
    {CK_BUTTON,    ID_CHK_AUTOSTART,L"开机自启（登录 Windows 时自动运行）", BS_OWNERDRAW, 20, 712, 340, 24},
    {CK_BUTTON,    ID_BTN_OK,      L"确定",                 BS_OWNERDRAW | WS_GROUP, 176, 746,  96, 32},
    {CK_BUTTON,    ID_BTN_CANCEL,  L"取消",                 BS_OWNERDRAW,            284, 746,  96, 32},
};

// 应用当前缩放：重建字体 + 移动/改尺寸所有控件 + 输入框圆角区域
void ApplyScale(HWND dlg) {
    if (g_font) DeleteObject(g_font);
    if (g_fontHeader) DeleteObject(g_fontHeader);
    g_font = MakeFont(S(14), FW_NORMAL);
    g_fontHeader = MakeFont(S(13), FW_SEMIBOLD);
    for (const CtrlSpec& s : kSpecs) {
        HWND hw = GetDlgItem(dlg, s.id);
        if (!hw) continue;
        int x = S(s.x), y = S(s.y), w = S(s.w), h = S(s.h);
        MoveWindow(hw, x, y, w, h, TRUE);
        SendMessageW(hw, WM_SETFONT, (WPARAM)(s.kind == CK_HEADER ? g_fontHeader : g_font), TRUE);
        if (s.kind == CK_EDIT)
            SetWindowRgn(hw, CreateRoundRectRgn(0, 0, w + 1, h + 1, S(8), S(8)), TRUE);
    }
    RedrawWindow(dlg, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

void UpdateIdleEnabled(HWND dlg) {
    BOOL on = IsBtnChecked(dlg, ID_RADIO_IDLE);
    EnableWindow(GetDlgItem(dlg, ID_EDIT_IDLE), on);
    InvalidateRect(GetDlgItem(dlg, ID_EDIT_IDLE), nullptr, TRUE); // 重绘边框的禁用色
}

// 读编辑框：越界值钳到边界（原先静默丢弃，用户以为设置无效），并回写让用户看到实际生效值
int ReadInt(HWND dlg, int id, int lo, int hi, int cur) {
    wchar_t buf[32] = {0};
    if (GetDlgItemTextW(dlg, id, buf, 32) <= 0) return cur;
    int v = _wtoi(buf);
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    SetDlgItemInt(dlg, id, (UINT)v, FALSE);
    return v;
}

void OnOk(HWND dlg) {
    Config& cfg = g_app.cfg;

    if (IsBtnChecked(dlg, ID_RADIO_FOCUS))
        cfg.policy = ShowPolicy::InputFocus;
    else if (IsBtnChecked(dlg, ID_RADIO_IDLE))
        cfg.policy = ShowPolicy::IdleHide;
    else
        cfg.policy = ShowPolicy::Always;

    cfg.idleSeconds = ReadInt(dlg, ID_EDIT_IDLE, 1, 3600, cfg.idleSeconds);
    cfg.offsetX = ReadInt(dlg, ID_EDIT_OFFX, 0, 300, cfg.offsetX);
    cfg.offsetY = ReadInt(dlg, ID_EDIT_OFFY, 0, 300, cfg.offsetY);
    cfg.circle.enabled = IsBtnChecked(dlg, ID_CHK_CIRCLE);
    cfg.circle.size = ReadInt(dlg, ID_EDIT_SIZE, 8, 64, cfg.circle.size);
    cfg.circle.borderWidth = ReadInt(dlg, ID_EDIT_BORDER, 0, 10, cfg.circle.borderWidth);
    cfg.circle.opacity = ReadInt(dlg, ID_EDIT_OPACITY, 20, 100, cfg.circle.opacity);
    cfg.circle.colorZh = g_colPending[0];
    cfg.circle.colorYing = g_colPending[1];
    cfg.circle.colorEn = g_colPending[2];
    cfg.circle.colorCaps = g_colPending[3];
    cfg.fireworks.enabled = IsBtnChecked(dlg, ID_CHK_FIREWORKS);
    cfg.sand.enabled = IsBtnChecked(dlg, ID_CHK_SAND);
    cfg.sand.spawnPerTick = ReadInt(dlg, ID_EDIT_SPAWN, 1, 8, cfg.sand.spawnPerTick);
    cfg.sand.tiltMax = ReadInt(dlg, ID_EDIT_TILT, 0, 45, cfg.sand.tiltMax);
    FxStateCfg* fw[3] = { &cfg.fireworks.zh, &cfg.fireworks.en, &cfg.fireworks.caps };
    FxStateCfg* sd[3] = { &cfg.sand.zh, &cfg.sand.en, &cfg.sand.caps };
    for (int i = 0; i < 3; ++i) {
        fw[i]->mode = g_fxMode[0][i]; fw[i]->color = g_fxColor[0][i];
        sd[i]->mode = g_fxMode[1][i]; sd[i]->color = g_fxColor[1][i];
    }
    cfg.autostart = IsBtnChecked(dlg, ID_CHK_AUTOSTART);

    ConfigSave(cfg, ConfigPath());
    if (g_app.hMain) PostMessageW(g_app.hMain, WM_APP_CONFIG_APPLIED, 0, 0);
}

LRESULT CALLBACK SettingsProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wp;
        RECT r;
        GetClientRect(hwnd, &r);
        FillRect(hdc, &r, g_dlgBrush);
        return 1;
    }
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORBTN: {
        HDC hdc = (HDC)wp;
        SetTextColor(hdc, kTextBg);
        SetBkColor(hdc, kDlgBg);
        SetBkMode(hdc, TRANSPARENT);
        return (LRESULT)g_dlgBrush;
    }
    case WM_CTLCOLORSTATIC: {
        // owner-draw 静态不走这里；仅禁用态 EDIT 会走到（返回输入框底色保持观感一致）
        HDC hdc = (HDC)wp;
        SetTextColor(hdc, RGB(100, 100, 100));
        SetBkColor(hdc, kEditBg);
        return (LRESULT)g_editBrush;
    }
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wp;
        SetTextColor(hdc, kTextBg);
        SetBkColor(hdc, kEditBg);
        return (LRESULT)g_editBrush;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (HIWORD(wp) == BN_CLICKED) {
            if (id == ID_BTN_OK) { OnOk(hwnd); DestroyWindow(hwnd); return 0; }
            if (id == ID_BTN_CANCEL) { DestroyWindow(hwnd); return 0; }
            if (id >= ID_BTN_COL_ZH && id <= ID_BTN_COL_CAPS) {
                int idx = id - ID_BTN_COL_ZH;
                CtxChooseColorW cc{};
                cc.lStructSize = sizeof(cc);
                cc.hwndOwner = hwnd;
                cc.rgbResult = RGB((g_colPending[idx] >> 16) & 0xFF,
                                   (g_colPending[idx] >> 8) & 0xFF,
                                   g_colPending[idx] & 0xFF);
                cc.lpCustColors = g_custColors;
                cc.Flags = kCCRGBINIT | kCCSOLIDCOLOR | kCCANYCOLOR;
                if (ChooseColorW(&cc)) {
                    g_colPending[idx] = (GetRValue(cc.rgbResult) << 16) |
                                        (GetGValue(cc.rgbResult) << 8) | GetBValue(cc.rgbResult);
                    InvalidateRect(GetDlgItem(hwnd, id), nullptr, TRUE);
                }
                return 0;
            }
            if (id == ID_RADIO_ALWAYS || id == ID_RADIO_FOCUS || id == ID_RADIO_IDLE) {
                // 自绘单选无自动互斥：手动管理选中状态
                const int ids[3] = { ID_RADIO_ALWAYS, ID_RADIO_FOCUS, ID_RADIO_IDLE };
                for (int r : ids) SetBtnChecked(GetDlgItem(hwnd, r), r == id);
                UpdateIdleEnabled(hwnd);
                return 0;
            }
            if (id == ID_CHK_AUTOSTART || id == ID_CHK_FIREWORKS || id == ID_CHK_SAND ||
                id == ID_CHK_CIRCLE) {
                // 自绘复选无自动切换：手动翻转勾选状态
                HWND hb = GetDlgItem(hwnd, id);
                SetBtnChecked(hb, !GetBtnChecked(hb));
                return 0;
            }
            {
                int eff = 0, idx = 0;
                if (FxBtnMap(id, eff, idx)) { // 模式按钮：不显示→彩色→纯色 循环
                    g_fxMode[eff][idx] = (g_fxMode[eff][idx] + 1) % 3;
                    RefreshFxModeBtn(hwnd, eff, idx);
                    return 0;
                }
                if (FxChipMap(id, eff, idx)) { // 色块：选色并自动切为纯色
                    CtxChooseColorW cc{};
                    cc.lStructSize = sizeof(cc);
                    cc.hwndOwner = hwnd;
                    cc.rgbResult = RGB((g_fxColor[eff][idx] >> 16) & 0xFF,
                                       (g_fxColor[eff][idx] >> 8) & 0xFF,
                                       g_fxColor[eff][idx] & 0xFF);
                    cc.lpCustColors = g_custColors;
                    cc.Flags = kCCRGBINIT | kCCSOLIDCOLOR | kCCANYCOLOR;
                    if (ChooseColorW(&cc)) {
                        g_fxColor[eff][idx] = (GetRValue(cc.rgbResult) << 16) |
                                              (GetGValue(cc.rgbResult) << 8) |
                                              GetBValue(cc.rgbResult);
                        g_fxMode[eff][idx] = 2;
                        RefreshFxModeBtn(hwnd, eff, idx);
                        InvalidateRect(GetDlgItem(hwnd, id), nullptr, TRUE);
                    }
                    return 0;
                }
            }
        }
        return 0;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
        if (!dis) return 0;
        if (dis->CtlType == ODT_STATIC) {
            if (dis->CtlID == ID_SEP_POLICY || dis->CtlID == ID_SEP_OFFSET ||
                dis->CtlID == ID_SEP_FX || dis->CtlID == ID_SEP_MISC) {
                DrawSeparator(dis);
            } else {
                DrawStaticText(dis);
            }
            return TRUE;
        }
        if (dis->CtlType == ODT_BUTTON) {
            // 纯 BS_OWNERDRAW 不会置 ODS_CHECKED，选中态读自管属性
            bool checked = GetBtnChecked(dis->hwndItem);
            int eff = 0, idx = 0;
            if (FxBtnMap(dis->CtlID, eff, idx)) { // 模式循环按钮：次按钮样式
                DrawButton(dis, false);
                return TRUE;
            }
            if (FxChipMap(dis->CtlID, eff, idx)) { // 纯色色块
                DrawColorChip(dis, g_fxColor[eff][idx]);
                return TRUE;
            }
            if (dis->CtlID == ID_CHK_AUTOSTART || dis->CtlID == ID_CHK_FIREWORKS ||
                dis->CtlID == ID_CHK_SAND || dis->CtlID == ID_CHK_CIRCLE) {
                DrawCheckBox(dis, checked);
                return TRUE;
            }
            if (dis->CtlID == ID_RADIO_ALWAYS || dis->CtlID == ID_RADIO_FOCUS ||
                dis->CtlID == ID_RADIO_IDLE) {
                DrawRadioButton(dis, checked);
                return TRUE;
            }
            if (dis->CtlID >= ID_BTN_COL_ZH && dis->CtlID <= ID_BTN_COL_CAPS) {
                DrawColorSwatch(dis, dis->CtlID - ID_BTN_COL_ZH);
                return TRUE;
            }
            if (dis->CtlID == ID_BTN_OK) {
                DrawButton(dis, true);
                return TRUE;
            }
            if (dis->CtlID == ID_BTN_CANCEL) {
                DrawButton(dis, false);
                return TRUE;
            }
        }
        return 0;
    }
    case WM_DPICHANGED: {
        // 窗口被拖到不同缩放的显示器：按系统建议的新矩形改尺寸并重排全部控件
        float newScale = (float)HIWORD(wp) / 96.0f;
        if (newScale != g_uiScale) {
            g_uiScale = newScale;
            RECT* prc = reinterpret_cast<RECT*>(lp);
            SetWindowPos(hwnd, nullptr, prc->left, prc->top,
                         prc->right - prc->left, prc->bottom - prc->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            ApplyScale(hwnd);
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) { DestroyWindow(hwnd); return 0; }
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

void ShowSettingsDialog(HWND owner) {
    (void)owner; // 主窗口隐藏无尺寸，对话框改为在光标所在显示器居中

    // 重入守卫：设置对话框的模态循环仍会分发托盘消息，期间再次点「设置」会嵌套打开
    // 第二个对话框并共享/双删全局 GDI 资源，此处直接忽略重复打开
    if (g_hSettingsDlg && IsWindow(g_hSettingsDlg)) {
        SetForegroundWindow(g_hSettingsDlg);
        return;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = SettingsProc;
    wc.hInstance = g_app.hInst;
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"ImeIndicatorSettings";
    RegisterClassExW(&wc);

    // 对话框将居中于光标所在显示器：按该屏 DPI 定初始缩放
    POINT pt{};
    GetCursorPos(&pt);
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    UINT dpi = 96;
    {
        HMODULE hSh = LoadLibraryW(L"shcore.dll");
        if (hSh) {
            typedef HRESULT(WINAPI* GetDpiFn)(HMONITOR, int, UINT*, UINT*);
            GetDpiFn f = (GetDpiFn)GetProcAddress(hSh, "GetDpiForMonitor");
            UINT dx = 96, dy = 96;
            if (f && f(mon, 0 /*MDT_EFFECTIVE_DPI*/, &dx, &dy) == S_OK) dpi = dx;
        }
    }
    g_uiScale = (float)dpi / 96.0f;

    g_dlgBrush = CreateSolidBrush(kDlgBg);
    g_editBrush = CreateSolidBrush(kEditBg);

    // 客户区 400x792（逻辑）：用 AdjustWindowRectExForDpi 换算外框尺寸
    // WS_CLIPCHILDREN：对话框自绘背景时裁剪子窗口区域，否则 FillRect 会盖掉已画的自绘控件
    DWORD dlgStyle = WS_CAPTION | WS_SYSMENU | WS_OVERLAPPED | WS_CLIPCHILDREN;
    RECT rc{0, 0, S(400), S(792)};
    {
        HMODULE hUser = GetModuleHandleW(L"user32.dll");
        typedef BOOL(WINAPI* AdjFn)(LPRECT, DWORD, BOOL, DWORD, UINT);
        AdjFn adj = hUser ? (AdjFn)GetProcAddress(hUser, "AdjustWindowRectExForDpi") : nullptr;
        if (adj) adj(&rc, dlgStyle, FALSE, 0, dpi);
        else AdjustWindowRectEx(&rc, dlgStyle, FALSE, 0);
    }
    HWND dlg = CreateWindowExW(0, L"ImeIndicatorSettings", L"输入法指示器 - 设置",
                               dlgStyle, 0, 0,
                               rc.right - rc.left, rc.bottom - rc.top,
                               owner, nullptr, g_app.hInst, nullptr);
    if (!dlg) return;
    g_hSettingsDlg = dlg;

    // Win11 圆角 + 深色标题栏（动态加载 dwmapi；旧系统忽略）
    HMODULE hDwm = LoadLibraryW(L"dwmapi.dll");
    if (hDwm) {
        typedef HRESULT(WINAPI* DwmFn)(HWND, DWORD, LPCVOID, DWORD);
        DwmFn dwm = (DwmFn)GetProcAddress(hDwm, "DwmSetWindowAttribute");
        if (dwm) {
            int corner = 2; // DWMWCP_ROUND
            dwm(dlg, 33, &corner, sizeof(corner)); // DWMWA_WINDOW_CORNER_PREFERENCE
            BOOL dark = TRUE;
            HRESULT hr = dwm(dlg, 20, &dark, sizeof(dark)); // DWMWA_USE_IMMERSIVE_DARK_MODE
            if (FAILED(hr)) dwm(dlg, 19, &dark, sizeof(dark)); // 旧版本号
        }
    }

    for (const CtrlSpec& s : kSpecs) {
        const wchar_t* cls = s.kind == CK_EDIT    ? L"EDIT"
                             : s.kind == CK_BUTTON ? L"BUTTON"
                                                   : L"STATIC";
        CreateCtrl(dlg, cls, s.text, s.style, S(s.x), S(s.y), S(s.w), S(s.h), s.id);
    }
    ApplyScale(dlg); // 挂字体（标题用标题字体）+ 按缩放定位

    // 填充当前值
    const Config& cfg = g_app.cfg;
    {
        const int ids[3] = { ID_RADIO_ALWAYS, ID_RADIO_FOCUS, ID_RADIO_IDLE };
        int sel = (cfg.policy == ShowPolicy::Always) ? ID_RADIO_ALWAYS :
                  (cfg.policy == ShowPolicy::InputFocus) ? ID_RADIO_FOCUS : ID_RADIO_IDLE;
        for (int r : ids) SetBtnChecked(GetDlgItem(dlg, r), r == sel);
    }
    SetDlgItemInt(dlg, ID_EDIT_IDLE, (UINT)cfg.idleSeconds, FALSE);
    SetDlgItemInt(dlg, ID_EDIT_OFFX, (UINT)cfg.offsetX, FALSE);
    SetDlgItemInt(dlg, ID_EDIT_OFFY, (UINT)cfg.offsetY, FALSE);
    SetBtnChecked(GetDlgItem(dlg, ID_CHK_CIRCLE), cfg.circle.enabled);
    SetDlgItemInt(dlg, ID_EDIT_SIZE, (UINT)cfg.circle.size, FALSE);
    SetDlgItemInt(dlg, ID_EDIT_BORDER, (UINT)cfg.circle.borderWidth, FALSE);
    SetDlgItemInt(dlg, ID_EDIT_OPACITY, (UINT)cfg.circle.opacity, FALSE);
    g_colPending[0] = cfg.circle.colorZh;
    g_colPending[1] = cfg.circle.colorYing;
    g_colPending[2] = cfg.circle.colorEn;
    g_colPending[3] = cfg.circle.colorCaps;
    SetBtnChecked(GetDlgItem(dlg, ID_CHK_FIREWORKS), cfg.fireworks.enabled);
    SetBtnChecked(GetDlgItem(dlg, ID_CHK_SAND), cfg.sand.enabled);
    {
        const FxStateCfg* fw[3] = { &cfg.fireworks.zh, &cfg.fireworks.en, &cfg.fireworks.caps };
        const FxStateCfg* sd[3] = { &cfg.sand.zh, &cfg.sand.en, &cfg.sand.caps };
        for (int i = 0; i < 3; ++i) {
            g_fxMode[0][i] = fw[i]->mode; g_fxColor[0][i] = fw[i]->color;
            g_fxMode[1][i] = sd[i]->mode; g_fxColor[1][i] = sd[i]->color;
            RefreshFxModeBtn(dlg, 0, i);
            RefreshFxModeBtn(dlg, 1, i);
        }
    }
    SetDlgItemInt(dlg, ID_EDIT_SPAWN, (UINT)cfg.sand.spawnPerTick, FALSE);
    SetDlgItemInt(dlg, ID_EDIT_TILT, (UINT)cfg.sand.tiltMax, FALSE);
    SetBtnChecked(GetDlgItem(dlg, ID_CHK_AUTOSTART), cfg.autostart);
    UpdateIdleEnabled(dlg);

    // 在光标所在显示器的工作区内居中（避免隐藏 owner 导致的飞出屏幕）
    RECT wr{};
    GetWindowRect(dlg, &wr);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    RECT wa{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    if (GetMonitorInfoW(mon, &mi)) wa = mi.rcWork;
    int w = wr.right - wr.left, h = wr.bottom - wr.top;
    int x = wa.left + ((wa.right - wa.left) - w) / 2;
    int y = wa.top + ((wa.bottom - wa.top) - h) / 2;
    SetWindowPos(dlg, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

    ShowWindow(dlg, SW_SHOW);
    UpdateWindow(dlg);

    // 模态消息循环（期间主窗口消息仍被分发）
    MSG msg{};
    bool quitRequested = false;
    while (IsWindow(dlg)) {
        BOOL r = GetMessageW(&msg, nullptr, 0, 0);
        if (r == 0) { quitRequested = true; break; } // WM_QUIT（如托盘退出）：跳出后转发
        if (r == -1) break;
        // 自绘按钮无 BS_DEFPUSHBUTTON：手动支持回车=确定
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) {
            SendMessageW(dlg, WM_COMMAND, MAKEWPARAM(ID_BTN_OK, BN_CLICKED),
                         (LPARAM)GetDlgItem(dlg, ID_BTN_OK));
            continue;
        }
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    // 不转发则 WM_QUIT 被本循环吞掉，wWinMain 的 GetMessage 会永远阻塞（进程挂死）
    if (quitRequested) PostQuitMessage((int)msg.wParam);

    g_hSettingsDlg = nullptr;
    if (g_font) { DeleteObject(g_font); g_font = nullptr; }
    if (g_fontHeader) { DeleteObject(g_fontHeader); g_fontHeader = nullptr; }
    if (g_dlgBrush) { DeleteObject(g_dlgBrush); g_dlgBrush = nullptr; }
    if (g_editBrush) { DeleteObject(g_editBrush); g_editBrush = nullptr; }
}

} // namespace imeind
