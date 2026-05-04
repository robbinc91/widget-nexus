#pragma once

#include <windows.h>

// Older MinGW headers omit Per-Monitor v2 notifications.
#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

void InitProcessDpi();

void SetLayoutDpiFromWindow(HWND hwnd);

UINT GetLayoutDpi();

int ScaleByDpi(int px96);
