#include "app.h"
#include "config.h"
#include "ime_monitor.h"
#include "floating.h"
#include "fx.h"
#include "sand.h"
#include "settings.h"
#include "log.h"

#include <gdiplus.h>
#include <shellapi.h>
#include <commctrl.h>
#include <cmath>
#include <cstdlib>
#include <string>
#include <cstring>
#include <cwchar>

// MinGW-w64 头文件缺失常量（MS SDK 值）
#ifndef MIIM_BITMAP
#define MIIM_BITMAP 0x00000080
#endif

namespace imeind {
App g_app;
ULONG_PTR g_gdiplusToken = 0;
ImeMonitor g_imeMonitor;
} // namespace imeind

using namespace imeind;

namespace {

constexpr UINT_PTR kTrayId = 1;
constexpr UINT IDM_TOGGLE = 1;
constexpr UINT IDM_SETTINGS = 2;
constexpr UINT IDM_AUTOSTART = 3;
constexpr UINT IDM_EXIT = 4;

const wchar_t* kMainClass = L"ImeIndicatorMain";
const wchar_t* kMutexName = L"Local\\ImeIndicator_SingleInstance";
const wchar_t* kRunValue = L"ImeIndicator";

Gdiplus::GraphicsPath* MakeRoundRect(float x, float y, float w, float h, float r) {
    Gdiplus::GraphicsPath* p = new Gdiplus::GraphicsPath();
    float d = r * 2.0f;
    p->AddArc(x, y, d, d, 180, 90);
    p->AddArc(x + w - d, y, d, d, 270, 90);
    p->AddArc(x + w - d, y + h - d, d, d, 0, 90);
    p->AddArc(x, y + h - d, d, d, 90, 90);
    p->CloseFigure();
    return p;
}

// 托盘图标：随状态变色 + 文字
HICON MakeStateIcon(const ImeState& s) {
    const int sz = 16;
    // 注意：PixelFormat32bppARGB 在 MinGW-w64 里是宏，不能加 Gdiplus:: 限定
    Gdiplus::Bitmap bmp(sz, sz, PixelFormat32bppARGB);
    Gdiplus::Graphics g(&bmp);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    Gdiplus::Color bg(255, 96, 108, 128); // 默认灰 = EN
    if (s.chinese) bg = Gdiplus::Color(255, 46, 125, 90);      // 绿 = 中文
    else if (!s.englishLayout) bg = Gdiplus::Color(255, 170, 130, 50); // 琥珀 = 英文模式

    Gdiplus::GraphicsPath* p = MakeRoundRect(1, 1, 14, 14, 4);
    Gdiplus::SolidBrush br(bg);
    g.FillPath(&br, p);
    delete p;

    std::wstring letter = s.chinese ? L"中" : (s.englishLayout ? L"E" : L"英");
    Gdiplus::Font f(L"Microsoft YaHei", 9.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::StringFormat sf;
    sf.SetAlignment(Gdiplus::StringAlignmentCenter);
    sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    Gdiplus::RectF rc(0, 0, (Gdiplus::REAL)sz, (Gdiplus::REAL)sz);
    Gdiplus::SolidBrush wb(Gdiplus::Color(255, 255, 255, 255));
    g.DrawString(letter.c_str(), (INT)letter.size(), &f, rc, &sf, &wb);

    HICON icon = nullptr;
    bmp.GetHICON(&icon);
    return icon;
}

NOTIFYICONDATAW MakeNid() {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_app.hMain;
    nid.uID = (UINT)kTrayId;
    nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    nid.uCallbackMessage = WM_APP_TRAY;
    return nid;
}

void AddTrayIcon() {
    NOTIFYICONDATAW nid = MakeNid();
    nid.hIcon = MakeStateIcon(g_app.state);
    wcscpy_s(nid.szTip, 128, L"输入法指示器");
    Shell_NotifyIconW(NIM_ADD, &nid);
    g_app.hTrayIcon = nid.hIcon;
}

void UpdateTrayIcon(const ImeState& s) {
    HICON ni = MakeStateIcon(s);
    NOTIFYICONDATAW nid = MakeNid();
    nid.uFlags = NIF_ICON | NIF_TIP;
    nid.hIcon = ni;
    std::wstring tip = L"输入法指示器 | " + s.langText + (s.capsLock ? L" | 大写" : L"");
    wcsncpy_s(nid.szTip, 128, tip.c_str(), _TRUNCATE);
    if (Shell_NotifyIconW(NIM_MODIFY, &nid)) {
        if (g_app.hTrayIcon) DestroyIcon(g_app.hTrayIcon);
        g_app.hTrayIcon = ni;
    } else {
        DestroyIcon(ni); // 修改失败则保留旧图标
    }
}

void RemoveTrayIcon() {
    NOTIFYICONDATAW nid = MakeNid();
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

bool SetAutostart(bool on) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER,
                        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                        0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return false;
    LSTATUS st;
    if (on) {
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring v = L"\"" + std::wstring(path) + L"\"";
        st = RegSetValueExW(key, kRunValue, 0, REG_SZ,
                            (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t)));
    } else {
        st = RegDeleteValueW(key, kRunValue);
        if (st == ERROR_FILE_NOT_FOUND) st = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    return st == ERROR_SUCCESS;
}

FloatingWindow* FloatWnd() {
    if (!g_app.hFloat) return nullptr;
    return reinterpret_cast<FloatingWindow*>(GetWindowLongPtrW(g_app.hFloat, GWLP_USERDATA));
}

void ToggleFloat() {
    FloatingWindow* f = FloatWnd();
    if (f) f->Show(!f->IsVisible());
}

// 托盘菜单图标：Segoe MDL2 Assets 图标字体（Win10 原生风格）→ 32bpp DIB HBITMAP
// kind: 0=显示/隐藏 1=设置 2=自启未开启 3=退出 4=自启已开启
HBITMAP MakeMenuIcon(int kind) {
    const int sz = 16;
    const unsigned char neutral[3] = {60, 64, 72};
    const unsigned char accent[3] = {0, 122, 204};
    struct Item { const wchar_t* glyph; float px; const unsigned char* rgb; };
    const Item items[5] = {
        { L"", 12.0f, neutral }, // Eye
        { L"", 13.0f, neutral }, // Settings
        { L"", 13.0f, neutral }, // PowerButton
        { L"", 13.0f, neutral }, // ChromeClose
        { L"", 13.0f, accent  }, // PowerButton（开启态用强调蓝）
    };
    const Item& it = items[kind];
    Gdiplus::Bitmap bmp(sz, sz, PixelFormat32bppARGB);
    Gdiplus::Graphics g(&bmp);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
    Gdiplus::Font f(L"Segoe MDL2 Assets", it.px, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::StringFormat sf;
    sf.SetAlignment(Gdiplus::StringAlignmentCenter);
    sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    Gdiplus::SolidBrush br(Gdiplus::Color(255, it.rgb[0], it.rgb[1], it.rgb[2]));
    Gdiplus::RectF rc(0, 0, sz, sz);
    g.DrawString(it.glyph, 1, &f, rc, &sf, &br);
    HBITMAP bmpOut = nullptr;
    bmp.GetHBITMAP(Gdiplus::Color(0, 0, 0, 0), &bmpOut);
    if (!bmpOut) return nullptr;

    // 菜单按预乘 alpha 渲染 32bpp 位图，GetHBITMAP 返回的是直通 alpha，原地转换
    BITMAP bm{};
    if (GetObjectW(bmpOut, sizeof(bm), &bm) && bm.bmBits && bm.bmWidth * bm.bmHeight <= sz * sz)
        PremultiplyDib(bm.bmBits, sz, sz);
    return bmpOut;
}

void ShowTrayMenu(HWND hwnd) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, IDM_TOGGLE, L"显示 / 隐藏悬浮窗");
    AppendMenuW(menu, MF_STRING, IDM_SETTINGS, L"设置...");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (g_app.cfg.autostart ? MF_CHECKED : 0), IDM_AUTOSTART, L"开机自启");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_EXIT, L"退出");

    // 菜单图标（MIIM_BITMAP + 32bpp DIB：开机自启项按开/关用灰色/强调蓝电源图标区分）
    HBITMAP icons[4] = { MakeMenuIcon(0), MakeMenuIcon(1),
                         MakeMenuIcon(g_app.cfg.autostart ? 4 : 2), MakeMenuIcon(3) };
    const UINT ids[4] = { IDM_TOGGLE, IDM_SETTINGS, IDM_AUTOSTART, IDM_EXIT };
    for (int i = 0; i < 4; ++i) {
        MENUITEMINFOW mii{};
        mii.cbSize = sizeof(mii);
        mii.fMask = MIIM_BITMAP;
        mii.hbmpItem = icons[i];
        SetMenuItemInfoW(menu, ids[i], FALSE, &mii);
    }

    POINT p{};
    GetCursorPos(&p);
    SetForegroundWindow(hwnd);
    // 不用 TPM_RETURNCMD：让菜单项通过 WM_COMMAND 到达 MainProc
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, p.x, p.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
    for (HBITMAP ic : icons)
        if (ic) DeleteObject(ic);
    PostMessageW(hwnd, WM_NULL, 0, 0);
}

LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_APP_IME_STATE:
        g_app.state = g_imeMonitor.GetState();
        LogMsg(L"main: IME state applied lang=%ls caps=%d", g_app.state.langText.c_str(),
               (int)g_app.state.capsLock);
        if (FloatingWindow* f = FloatWnd()) f->UpdateState(g_app.state);
        UpdateTrayIcon(g_app.state);
        return 0;

    case WM_APP_CONFIG_APPLIED:
        SetAutostart(g_app.cfg.autostart);
        if (FloatingWindow* f = FloatWnd()) f->ApplyConfig();
        return 0;

    case WM_APP_SHOW:
        if (FloatingWindow* f = FloatWnd()) f->Show(true);
        return 0;

    case WM_APP_EXIT:
        LogMsg(L"main: WM_APP_EXIT");
        DestroyWindow(hwnd);
        return 0;

    case WM_APP_TRAY:
        if (lp == WM_LBUTTONUP) ToggleFloat();
        else if (lp == WM_RBUTTONUP) ShowTrayMenu(hwnd);
        return 0;

    case WM_COMMAND:
        if (HIWORD(wp) == 0) {
            switch (LOWORD(wp)) {
            case IDM_TOGGLE: ToggleFloat(); break;
            case IDM_SETTINGS: ShowSettingsDialog(hwnd); break;
            case IDM_AUTOSTART:
                g_app.cfg.autostart = !g_app.cfg.autostart;
                SetAutostart(g_app.cfg.autostart);
                ConfigSave(g_app.cfg, ConfigPath());
                break;
            case IDM_EXIT: PostMessageW(hwnd, WM_APP_EXIT, 0, 0); break;
            }
        }
        return 0;

    case WM_DESTROY:
        RemoveTrayIcon();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    // DPI 感知（Win10 1703+ 用 PerMonitorV2，旧系统回退）
    HMODULE hUser = GetModuleHandleW(L"user32.dll");
    if (hUser) {
        typedef BOOL(WINAPI* SetDpiFn)(void*);
        SetDpiFn fn = (SetDpiFn)GetProcAddress(hUser, "SetProcessDpiAwarenessContext");
        if (fn) fn((void*)-4); // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
        else SetProcessDPIAware();
    }

    // 单实例：已运行时只唤出悬浮窗
    HANDLE hMutex = CreateMutexW(nullptr, FALSE, kMutexName);
    if (hMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        LogInit();
        LogMsg(L"ANOTHER INSTANCE RUNNING - this process exits (old tray instance may hold the log)");
        LogClose();
        HWND h = FindWindowW(kMainClass, nullptr);
        if (h) PostMessageW(h, WM_APP_SHOW, 0, 0);
        CloseHandle(hMutex);
        return 0;
    }

    srand((unsigned)GetTickCount64()); // 粒子特效随机数种子（不播种则每次运行序列相同）

    g_app.hInst = hInst;
    LogInit();
    LogMsg(L"wWinMain: single instance ok");

    // 初始化公共控件类（配合嵌入的 ComCtl32 v6 manifest 启用视觉样式）
    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    Config cfg;
    ConfigLoad(cfg, ConfigPath());
    g_app.cfg = cfg;
    LogMsg(L"config: policy=%d offset=%d,%d autostart=%d idle=%d fireworks=%d sand=%d",
           (int)cfg.policy, cfg.offsetX, cfg.offsetY,
           (int)cfg.autostart, cfg.idleSeconds, (int)cfg.fireworks.enabled, (int)cfg.sand.enabled);

    Gdiplus::GdiplusStartupInput gsi;
    Gdiplus::Status st = Gdiplus::GdiplusStartup(&g_gdiplusToken, &gsi, nullptr);
    LogMsg(L"gdiplus startup: %d", (int)st);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MainProc;
    wc.hInstance = hInst;
    wc.lpszClassName = kMainClass;
    RegisterClassExW(&wc);
    g_app.hMain = CreateWindowExW(0, kMainClass, L"ImeIndicator", WS_OVERLAPPED,
                                  0, 0, 0, 0, nullptr, nullptr, hInst, nullptr);
    LogMsg(L"main window: %p", g_app.hMain);

    FloatingWindow floatWnd;
    if (floatWnd.Create(hInst, g_app.hMain)) g_app.hFloat = floatWnd.Hwnd();
    LogMsg(L"float window: %p", g_app.hFloat);

    // 特效窗：各自带定时器，每帧直接读 g_app.cfg/state，无需外部驱动（与悬浮窗解耦）
    FireworksWindow fxWnd;
    if (fxWnd.Create(hInst, g_app.hMain, &floatWnd)) g_app.hFx = fxWnd.Hwnd();

    SandWindow sandWnd;
    if (sandWnd.Create(hInst, g_app.hMain, &floatWnd)) g_app.hSand = sandWnd.Hwnd();

    g_imeMonitor.Start(g_app.hMain);
    g_app.state = g_imeMonitor.GetState();

    AddTrayIcon();
    SetAutostart(g_app.cfg.autostart);
    LogMsg(L"tray icon added: %p", g_app.hTrayIcon);

    // 初始显示：不强制，由悬浮窗 33ms 定时器按显示策略驱动（首个 tick 内定位到光标旁）

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    LogMsg(L"message loop exit");
    g_imeMonitor.Stop();
    sandWnd.Destroy();
    fxWnd.Destroy();
    floatWnd.Destroy(); // 停掉 33ms 定时器并销毁窗口
    RemoveTrayIcon();
    if (g_app.hTrayIcon) { DestroyIcon(g_app.hTrayIcon); g_app.hTrayIcon = nullptr; }
    Gdiplus::GdiplusShutdown(g_gdiplusToken);
    CloseHandle(hMutex);
    LogClose();
    return (int)msg.wParam;
}
