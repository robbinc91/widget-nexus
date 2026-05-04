#include "dpi.hpp"

#ifndef PROCESS_PER_MONITOR_DPI_AWARE
#define PROCESS_PER_MONITOR_DPI_AWARE 2
#endif

static UINT g_layoutDpi = 96;

void InitProcessDpi() {
    // Avoid DPI_AWARENESS_CONTEXT / using+WINAPI — missing or fragile on older MinGW headers.
    typedef BOOL(WINAPI * SetProcessDpiAwarenessContextFn)(void*);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        auto* setCtx = reinterpret_cast<SetProcessDpiAwarenessContextFn>(
            reinterpret_cast<void*>(GetProcAddress(user32, "SetProcessDpiAwarenessContext")));
        if (setCtx) {
            void* perMonitorV2 = reinterpret_cast<void*>(static_cast<INT_PTR>(-4)); // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
            if (setCtx(perMonitorV2)) return;
        }
    }

    typedef HRESULT(WINAPI * SetProcessDpiAwarenessFn)(int);
    HMODULE shcore = LoadLibraryW(L"shcore.dll");
    if (!shcore) return;
    auto* setAware = reinterpret_cast<SetProcessDpiAwarenessFn>(
        reinterpret_cast<void*>(GetProcAddress(shcore, "SetProcessDpiAwareness")));
    if (setAware) (void)setAware(static_cast<int>(PROCESS_PER_MONITOR_DPI_AWARE));
    FreeLibrary(shcore);
}

void SetLayoutDpiFromWindow(HWND hwnd) {
    if (!hwnd) return;
    typedef UINT(WINAPI * GetDpiForWindowFn)(HWND);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) return;
    auto* getDpi = reinterpret_cast<GetDpiForWindowFn>(reinterpret_cast<void*>(GetProcAddress(user32, "GetDpiForWindow")));
    if (getDpi) {
        const UINT d = getDpi(hwnd);
        if (d > 0) g_layoutDpi = d;
        return;
    }
    HDC hdc = GetDC(hwnd);
    if (hdc) {
        g_layoutDpi = static_cast<UINT>(GetDeviceCaps(hdc, LOGPIXELSX));
        ReleaseDC(hwnd, hdc);
    }
}

UINT GetLayoutDpi() { return g_layoutDpi > 0 ? g_layoutDpi : 96; }

int ScaleByDpi(int px96) { return MulDiv(px96, static_cast<int>(GetLayoutDpi()), 96); }
