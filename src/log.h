#pragma once
#ifndef IMIND_LOG_H
#define IMIND_LOG_H

#include <windows.h>

namespace imeind {

// 极简文件日志：每次运行覆盖写 %APPDATA%\ImeIndicator\imeindicator.log（UTF-8 带 BOM）
void LogInit();
void LogMsg(const wchar_t* fmt, ...);   // printf 风格宽字符格式
void LogClose();

} // namespace imeind

#endif // IMIND_LOG_H
