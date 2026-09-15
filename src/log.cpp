#include "log.h"

#include <cstdarg>
#include <cwchar>
#include <string>

namespace imeind {

namespace {

HANDLE g_log = INVALID_HANDLE_VALUE;

std::wstring ExeDir() {
    wchar_t exe[MAX_PATH + 1] = {0};
    DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        std::wstring p = exe;
        size_t slash = p.find_last_of(L"\\/");
        if (slash != std::wstring::npos) p = p.substr(0, slash);
        return p;
    }
    return L".";
}

std::wstring AppDataDir() {
    wchar_t buf[MAX_PATH + 1] = {0};
    DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, (DWORD)MAX_PATH);
    if (n == 0 || n > MAX_PATH) GetTempPathW(MAX_PATH, buf);
    std::wstring p = buf;
    if (!p.empty() && p.back() != L'\\') p += L'\\';
    return p + L"ImeIndicator";
}

void Open() {
    // 优先写到 exe 同目录（和 build.log 放一起，方便查找）；失败再回退 APPDATA
    std::wstring path = ExeDir() + L"\\imeindicator.log";
    g_log = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (g_log == INVALID_HANDLE_VALUE) {
        std::wstring dir = AppDataDir();
        CreateDirectoryW(dir.c_str(), nullptr);
        path = dir + L"\\imeindicator.log";
        g_log = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    if (g_log != INVALID_HANDLE_VALUE) {
        // UTF-8 BOM，方便记事本识别
        const unsigned char bom[3] = {0xEF, 0xBB, 0xBF};
        DWORD wr = 0;
        WriteFile(g_log, bom, 3, &wr, nullptr);
    }
}

void WriteUtf8(const std::wstring& line) {
    if (g_log == INVALID_HANDLE_VALUE) Open();
    if (g_log == INVALID_HANDLE_VALUE) return;
    int len = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), (int)line.size(),
                                  nullptr, 0, nullptr, nullptr);
    if (len <= 0) return;
    std::string utf8(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, line.c_str(), (int)line.size(),
                        &utf8[0], len, nullptr, nullptr);
    DWORD wr = 0;
    WriteFile(g_log, utf8.data(), (DWORD)utf8.size(), &wr, nullptr);
}

} // namespace

void LogInit() {
    Open();
    wchar_t exe[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    LogMsg(L"==== ImeIndicator start (pid=%lu) exe=%ls ====", GetCurrentProcessId(), exe);
}

void LogMsg(const wchar_t* fmt, ...) {
    wchar_t buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vswprintf(buf, 2048, fmt, ap);
    va_end(ap);

    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t line[2304];
    swprintf(line, 2304, L"[%02u:%02u:%02u.%03u] %ls\r\n",
             st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, buf);
    WriteUtf8(line);
    OutputDebugStringW(line);
}

void LogClose() {
    if (g_log != INVALID_HANDLE_VALUE) {
        CloseHandle(g_log);
        g_log = INVALID_HANDLE_VALUE;
    }
}

} // namespace imeind
