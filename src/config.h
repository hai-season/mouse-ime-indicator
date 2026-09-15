#pragma once
#ifndef IMIND_CONFIG_H
#define IMIND_CONFIG_H

#include <string>
#include "app.h"

namespace imeind {

std::wstring ConfigPath();                        // %APPDATA%\ImeIndicator\config.json
bool ConfigLoad(Config& cfg, const std::wstring& path);
bool ConfigSave(const Config& cfg, const std::wstring& path);

} // namespace imeind

#endif // IMIND_CONFIG_H
