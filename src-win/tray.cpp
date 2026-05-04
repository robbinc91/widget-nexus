#include "tray.hpp"

#include "globals.hpp"
#include "nexus_paint.hpp"

#include <shellapi.h>

void TrayAdd(HWND hWnd) {
    if (g_trayIconAdded) return;
    NOTIFYICONDATAA nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hWnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_APP_TRAY;
    if (!g_trayIcon) g_trayIcon = CreateNeonWlIcon(16);
    nid.hIcon = g_trayIcon ? g_trayIcon : LoadIconA(nullptr, IDI_APPLICATION);
    lstrcpyA(nid.szTip, "Widget Nexus — right-click for menu");
    Shell_NotifyIconA(NIM_ADD, &nid);
    g_trayIconAdded = true;
}

void TrayRemove() {
    if (!g_trayIconAdded) return;
    NOTIFYICONDATAA nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_nexusHwnd;
    nid.uID = 1;
    Shell_NotifyIconA(NIM_DELETE, &nid);
    g_trayIconAdded = false;
}
