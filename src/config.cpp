#include "config.h"

#include <windows.h>
#include <vector>
#include <cwchar>

namespace imeind {

std::wstring ConfigPath() {
    wchar_t buf[MAX_PATH + 1] = {0};
    DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, (DWORD)(MAX_PATH));
    if (n == 0 || n > MAX_PATH) {
        GetTempPathW(MAX_PATH, buf);
    }
    std::wstring p = buf;
    if (!p.empty() && p.back() != L'\\') p += L'\\';
    p += L"ImeIndicator\\config.json";
    return p;
}

namespace {

// —— 极简 JSON 解析器（仅支持扁平对象：字符串 / 整数 / 布尔；字符串不支持转义） ——
struct JsonParser {
    const wchar_t* s;
    size_t pos;
    size_t len;

    explicit JsonParser(const std::wstring& t) : s(t.c_str()), pos(0), len(t.size()) {}

    void SkipWs() {
        while (pos < len && (s[pos] == L' ' || s[pos] == L'\t' || s[pos] == L'\r' || s[pos] == L'\n'))
            ++pos;
    }
    bool Eat(wchar_t c) {
        SkipWs();
        if (pos < len && s[pos] == c) { ++pos; return true; }
        return false;
    }
    bool EatString(std::wstring& out) {
        SkipWs();
        if (pos >= len || s[pos] != L'"') return false;
        ++pos;
        size_t start = pos;
        while (pos < len && s[pos] != L'"') ++pos;
        if (pos >= len) return false;
        out.assign(s + start, pos - start);
        ++pos;
        return true;
    }
    bool EatInt(int& val) {
        SkipWs();
        if (pos >= len || (s[pos] != L'-' && (s[pos] < L'0' || s[pos] > L'9'))) return false;
        bool neg = false;
        if (s[pos] == L'-') { neg = true; ++pos; }
        long long v = 0;
        bool any = false;
        while (pos < len && s[pos] >= L'0' && s[pos] <= L'9') {
            v = v * 10 + (s[pos] - L'0');
            ++pos;
            any = true;
        }
        if (!any) return false;
        val = (int)(neg ? -v : v);
        return true;
    }
    bool EatBool(bool& val) {
        SkipWs();
        if (pos + 4 <= len && wcsncmp(s + pos, L"true", 4) == 0) { pos += 4; val = true; return true; }
        if (pos + 5 <= len && wcsncmp(s + pos, L"false", 5) == 0) { pos += 5; val = false; return true; }
        return false;
    }
    void SkipValue() {
        SkipWs();
        if (pos >= len) return;
        if (s[pos] == L'"') { std::wstring t; EatString(t); return; }
        if (s[pos] == L't' || s[pos] == L'f') { bool b; EatBool(b); return; }
        int i;
        EatInt(i);
    }
};

void Clamp(Config& cfg) {
    if ((int)cfg.policy < 0 || (int)cfg.policy > 2) cfg.policy = ShowPolicy::Always;
    if (cfg.offsetX < 0) cfg.offsetX = 0;
    if (cfg.offsetX > 300) cfg.offsetX = 300;
    if (cfg.offsetY < 0) cfg.offsetY = 0;
    if (cfg.offsetY > 300) cfg.offsetY = 300;
    if (cfg.idleSeconds < 1) cfg.idleSeconds = 1;
    if (cfg.idleSeconds > 3600) cfg.idleSeconds = 3600;
    if (cfg.circle.size < 8) cfg.circle.size = 8;
    if (cfg.circle.size > 64) cfg.circle.size = 64;
    if (cfg.circle.borderWidth < 0) cfg.circle.borderWidth = 0;
    if (cfg.circle.borderWidth > 10) cfg.circle.borderWidth = 10;
    if (cfg.circle.opacity < 20) cfg.circle.opacity = 20;
    if (cfg.circle.opacity > 100) cfg.circle.opacity = 100;
    if (cfg.sand.spawnPerTick < 1) cfg.sand.spawnPerTick = 1;
    if (cfg.sand.spawnPerTick > 8) cfg.sand.spawnPerTick = 8;
    if (cfg.sand.tiltMax < 0) cfg.sand.tiltMax = 0;
    if (cfg.sand.tiltMax > 45) cfg.sand.tiltMax = 45;
    auto clampFx = [](FxStateCfg& f) {
        if (f.mode < 0 || f.mode > 2) f.mode = 0;
        f.color &= 0xFFFFFF;
    };
    clampFx(cfg.fireworks.zh); clampFx(cfg.fireworks.en); clampFx(cfg.fireworks.caps);
    clampFx(cfg.sand.zh); clampFx(cfg.sand.en); clampFx(cfg.sand.caps);
    cfg.circle.colorZh &= 0xFFFFFF;
    cfg.circle.colorYing &= 0xFFFFFF;
    cfg.circle.colorEn &= 0xFFFFFF;
    cfg.circle.colorCaps &= 0xFFFFFF;
}

} // namespace

bool ConfigLoad(Config& cfg, const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD size = GetFileSize(h, nullptr);
    std::vector<char> raw;
    if (size > 0 && size < 1024 * 1024) {
        raw.resize(size);
        DWORD rd = 0;
        ReadFile(h, raw.data(), size, &rd, nullptr);
        raw.resize(rd);
    }
    CloseHandle(h);
    if (raw.empty()) return false;

    std::wstring text;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, raw.data(), (int)raw.size(), nullptr, 0);
    if (wlen > 0) {
        text.resize(wlen);
        MultiByteToWideChar(CP_UTF8, 0, raw.data(), (int)raw.size(), &text[0], wlen);
    }
    if (text.empty()) return false;

    JsonParser p(text);
    if (!p.Eat(L'{')) return false;

    for (;;) {
        p.SkipWs();
        if (p.pos < p.len && p.s[p.pos] == L'}') { ++p.pos; break; }

        std::wstring key;
        if (!p.EatString(key)) break;
        if (!p.Eat(L':')) break;

        int iv = 0;
        bool bv = false;
        if (key == L"policy")       { if (p.EatInt(iv)) cfg.policy = (ShowPolicy)iv; }
        else if (key == L"offset_x"){ if (p.EatInt(iv)) cfg.offsetX = iv; }
        else if (key == L"offset_y"){ if (p.EatInt(iv)) cfg.offsetY = iv; }
        else if (key == L"idle_seconds") { if (p.EatInt(iv)) cfg.idleSeconds = iv; }
        // JSON 键名与旧版保持一致，配置文件无需迁移
        else if (key == L"circle_enabled"){ if (p.EatBool(bv)) cfg.circle.enabled = bv; }
        else if (key == L"circle_size")  { if (p.EatInt(iv)) cfg.circle.size = iv; }
        else if (key == L"border_width") { if (p.EatInt(iv)) cfg.circle.borderWidth = iv; }
        else if (key == L"opacity")      { if (p.EatInt(iv)) cfg.circle.opacity = iv; }
        else if (key == L"fireworks")    { if (p.EatBool(bv)) cfg.fireworks.enabled = bv; }
        else if (key == L"sand")         { if (p.EatBool(bv)) cfg.sand.enabled = bv; }
        else if (key == L"sand_spawn")   { if (p.EatInt(iv)) cfg.sand.spawnPerTick = iv; }
        else if (key == L"sand_tilt")    { if (p.EatInt(iv)) cfg.sand.tiltMax = iv; }
        else if (key == L"fw_zh_mode")   { if (p.EatInt(iv)) cfg.fireworks.zh.mode = iv; }
        else if (key == L"fw_zh_color")  { if (p.EatInt(iv)) cfg.fireworks.zh.color = iv; }
        else if (key == L"fw_en_mode")   { if (p.EatInt(iv)) cfg.fireworks.en.mode = iv; }
        else if (key == L"fw_en_color")  { if (p.EatInt(iv)) cfg.fireworks.en.color = iv; }
        else if (key == L"fw_caps_mode") { if (p.EatInt(iv)) cfg.fireworks.caps.mode = iv; }
        else if (key == L"fw_caps_color"){ if (p.EatInt(iv)) cfg.fireworks.caps.color = iv; }
        else if (key == L"sand_zh_mode") { if (p.EatInt(iv)) cfg.sand.zh.mode = iv; }
        else if (key == L"sand_zh_color"){ if (p.EatInt(iv)) cfg.sand.zh.color = iv; }
        else if (key == L"sand_en_mode") { if (p.EatInt(iv)) cfg.sand.en.mode = iv; }
        else if (key == L"sand_en_color"){ if (p.EatInt(iv)) cfg.sand.en.color = iv; }
        else if (key == L"sand_caps_mode"){ if (p.EatInt(iv)) cfg.sand.caps.mode = iv; }
        else if (key == L"sand_caps_color"){ if (p.EatInt(iv)) cfg.sand.caps.color = iv; }
        else if (key == L"color_zh")    { if (p.EatInt(iv)) cfg.circle.colorZh = iv; }
        else if (key == L"color_ying")  { if (p.EatInt(iv)) cfg.circle.colorYing = iv; }
        else if (key == L"color_en")    { if (p.EatInt(iv)) cfg.circle.colorEn = iv; }
        else if (key == L"color_caps")  { if (p.EatInt(iv)) cfg.circle.colorCaps = iv; }
        else if (key == L"autostart") { if (p.EatBool(bv)) cfg.autostart = bv; }
        else p.SkipValue();

        if (!p.Eat(L',')) {
            p.SkipWs();
            if (p.pos < p.len && p.s[p.pos] == L'}') { ++p.pos; break; }
            // 兼容历史 bug：旧版本保存时 sand_caps_color 后漏逗号，后续键仍照读
            if (p.pos >= p.len || p.s[p.pos] != L'"') break;
        }
    }

    Clamp(cfg);
    return true;
}

bool ConfigSave(const Config& cfg, const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    std::wstring dir = (slash == std::wstring::npos) ? L"." : path.substr(0, slash);
    CreateDirectoryW(dir.c_str(), nullptr);

    std::wstring text;
    text += L"{\n";
    text += L"  \"policy\": " + std::to_wstring((int)cfg.policy) + L",\n";
    text += L"  \"offset_x\": " + std::to_wstring(cfg.offsetX) + L",\n";
    text += L"  \"offset_y\": " + std::to_wstring(cfg.offsetY) + L",\n";
    text += L"  \"autostart\": " + std::wstring(cfg.autostart ? L"true" : L"false") + L",\n";
    text += L"  \"idle_seconds\": " + std::to_wstring(cfg.idleSeconds) + L",\n";
    text += L"  \"circle_enabled\": " + std::wstring(cfg.circle.enabled ? L"true" : L"false") + L",\n";
    text += L"  \"circle_size\": " + std::to_wstring(cfg.circle.size) + L",\n";
    text += L"  \"border_width\": " + std::to_wstring(cfg.circle.borderWidth) + L",\n";
    text += L"  \"opacity\": " + std::to_wstring(cfg.circle.opacity) + L",\n";
    text += L"  \"fireworks\": " + std::wstring(cfg.fireworks.enabled ? L"true" : L"false") + L",\n";
    text += L"  \"sand\": " + std::wstring(cfg.sand.enabled ? L"true" : L"false") + L",\n";
    text += L"  \"sand_spawn\": " + std::to_wstring(cfg.sand.spawnPerTick) + L",\n";
    text += L"  \"sand_tilt\": " + std::to_wstring(cfg.sand.tiltMax) + L",\n";
    text += L"  \"fw_zh_mode\": " + std::to_wstring(cfg.fireworks.zh.mode) + L",\n";
    text += L"  \"fw_zh_color\": " + std::to_wstring(cfg.fireworks.zh.color) + L",\n";
    text += L"  \"fw_en_mode\": " + std::to_wstring(cfg.fireworks.en.mode) + L",\n";
    text += L"  \"fw_en_color\": " + std::to_wstring(cfg.fireworks.en.color) + L",\n";
    text += L"  \"fw_caps_mode\": " + std::to_wstring(cfg.fireworks.caps.mode) + L",\n";
    text += L"  \"fw_caps_color\": " + std::to_wstring(cfg.fireworks.caps.color) + L",\n";
    text += L"  \"sand_zh_mode\": " + std::to_wstring(cfg.sand.zh.mode) + L",\n";
    text += L"  \"sand_zh_color\": " + std::to_wstring(cfg.sand.zh.color) + L",\n";
    text += L"  \"sand_en_mode\": " + std::to_wstring(cfg.sand.en.mode) + L",\n";
    text += L"  \"sand_en_color\": " + std::to_wstring(cfg.sand.en.color) + L",\n";
    text += L"  \"sand_caps_mode\": " + std::to_wstring(cfg.sand.caps.mode) + L",\n";
    text += L"  \"sand_caps_color\": " + std::to_wstring(cfg.sand.caps.color) + L",\n";
    text += L"  \"color_zh\": " + std::to_wstring(cfg.circle.colorZh) + L",\n";
    text += L"  \"color_ying\": " + std::to_wstring(cfg.circle.colorYing) + L",\n";
    text += L"  \"color_en\": " + std::to_wstring(cfg.circle.colorEn) + L",\n";
    text += L"  \"color_caps\": " + std::to_wstring(cfg.circle.colorCaps) + L"\n";
    text += L"}\n";

    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    int len = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0, nullptr, nullptr);
    std::vector<char> buf(len > 0 ? len : 1);
    if (len > 0) WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), buf.data(), len, nullptr, nullptr);

    DWORD wr = 0;
    bool ok = WriteFile(h, buf.data(), (DWORD)buf.size(), &wr, nullptr) && wr == buf.size();
    CloseHandle(h);
    return ok;
}

} // namespace imeind
