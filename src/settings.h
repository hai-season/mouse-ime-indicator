#pragma once
#ifndef IMIND_SETTINGS_H
#define IMIND_SETTINGS_H

#include <windows.h>

namespace imeind {

// 打开设置对话框（模态于 owner，运行自己的消息循环直到关闭）
void ShowSettingsDialog(HWND owner);

} // namespace imeind

#endif // IMIND_SETTINGS_H
