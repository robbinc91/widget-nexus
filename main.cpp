#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>

#include <algorithm>

#include "src-win/app_ids.hpp"
#include "src-win/command_runner.hpp"
#include "src-win/dpi.hpp"
#include "src-win/globals.hpp"
#include "src-win/layout.hpp"
#include "src-win/model.hpp"
#include "src-win/nexus_paint.hpp"
#include "src-win/nexus_ui.hpp"
#include "src-win/tray.hpp"

#include <string>

static LRESULT CALLBACK FloaterWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        const auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        const int idx = static_cast<int>(reinterpret_cast<INT_PTR>(cs->lpCreateParams));
        SetWindowLongPtr(hwnd, GWLP_USERDATA, static_cast<LONG_PTR>(idx));
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_NCHITTEST: {
        POINT pt{};
        pt.x = static_cast<int>(static_cast<short>(LOWORD(lParam)));
        pt.y = static_cast<int>(static_cast<short>(HIWORD(lParam)));
        ScreenToClient(hwnd, &pt);
        RECT rc{};
        GetClientRect(hwnd, &rc);
        if (!PtInRect(&rc, pt)) return DefWindowProc(hwnd, msg, wParam, lParam);
        const int cx = (rc.left + rc.right) / 2;
        const int cy = (rc.top + rc.bottom) / 2;
        const int R = (rc.right - rc.left) / 2;
        const long long dx = static_cast<long long>(pt.x - cx);
        const long long dy = static_cast<long long>(pt.y - cy);
        const long long dist2 = dx * dx + dy * dy;
        const long long rOuter = static_cast<long long>(R);
        const int innerR = std::max(1, R - FloaterDragRingPx());
        const long long rInner = static_cast<long long>(innerR);
        if (dist2 > rOuter * rOuter) return DefWindowProc(hwnd, msg, wParam, lParam);
        if (dist2 >= rInner * rInner) return HTCAPTION;
        return HTCLIENT;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        const HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc{};
        GetClientRect(hwnd, &rc);

        // Outer glow halo.
        HPEN halo = CreatePen(PS_SOLID, 1, LuxBlendRgb(kRgbNeonCyan, RGB(255, 255, 255), 0.18f));
        HPEN oldHalo = static_cast<HPEN>(SelectObject(hdc, halo));
        HBRUSH oldBr = static_cast<HBRUSH>(SelectObject(hdc, GetStockObject(NULL_BRUSH)));
        Ellipse(hdc, rc.left + 1, rc.top + 1, rc.right - 1, rc.bottom - 1);
        SelectObject(hdc, oldHalo);
        SelectObject(hdc, oldBr);
        DeleteObject(halo);

        if (HRGN clip = CreateEllipticRgn(rc.left, rc.top, rc.right, rc.bottom)) {
            SelectClipRgn(hdc, clip);
            LuxGradientVertical(hdc, rc, LuxBlendRgb(kRgbEdit, kRgbLuxPanelTop, 0.55f), LuxBlendRgb(kRgbEdit, kRgbDeep0, 0.35f));
            SelectClipRgn(hdc, nullptr);
            DeleteObject(clip);
        }
        if (g_penGlow && g_penFrame) {
            HBRUSH oldBr = static_cast<HBRUSH>(SelectObject(hdc, GetStockObject(NULL_BRUSH)));
            HPEN rim = CreatePen(PS_SOLID, 1, LuxBlendRgb(kRgbNeonDim, kRgbDeep0, 0.5f));
            HPEN oldP = static_cast<HPEN>(SelectObject(hdc, rim));
            Ellipse(hdc, rc.left, rc.top, rc.right, rc.bottom);
            SelectObject(hdc, g_penGlow);
            Ellipse(hdc, rc.left + 1, rc.top + 1, rc.right - 1, rc.bottom - 1);
            SelectObject(hdc, g_penFrame);
            Ellipse(hdc, rc.left + 2, rc.top + 2, rc.right - 2, rc.bottom - 2);
            SelectObject(hdc, oldP);
            SelectObject(hdc, oldBr);
            DeleteObject(rim);
        }
        const int idx = static_cast<int>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        std::string name = "Widget";
        if (idx >= 0 && idx < static_cast<int>(g_rows.size())) name = g_rows[static_cast<size_t>(idx)].w.name;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, kRgbNeonCyan);
        HFONT oldF = nullptr;
        if (g_fontUi) oldF = static_cast<HFONT>(SelectObject(hdc, g_fontUi));
        RECT textRc = rc;
        InflateRect(&textRc, -10, -10);
        DrawTextA(hdc, name.c_str(), -1, &textRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        if (CommandRunnerIsBusy()) {
            RECT badge{ rc.left + 6, rc.top + 6, rc.left + 18, rc.top + 18 };
            HBRUSH bb = CreateSolidBrush(RGB(220, 120, 0));
            FillRect(hdc, &badge, bb);
            DeleteObject(bb);
        }
        if (oldF) SelectObject(hdc, oldF);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONUP: {
        const int idx = static_cast<int>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        if (idx >= 0 && idx < static_cast<int>(g_rows.size())) RequestRunWidget(g_nexusHwnd, idx);
        return 0;
    }
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

static LRESULT CALLBACK GroupFloaterWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        const auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        const int idx = static_cast<int>(reinterpret_cast<INT_PTR>(cs->lpCreateParams));
        SetWindowLongPtr(hwnd, GWLP_USERDATA, static_cast<LONG_PTR>(idx));
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_NCHITTEST: {
        POINT pt{};
        pt.x = static_cast<int>(static_cast<short>(LOWORD(lParam)));
        pt.y = static_cast<int>(static_cast<short>(HIWORD(lParam)));
        ScreenToClient(hwnd, &pt);
        RECT rc{};
        GetClientRect(hwnd, &rc);
        if (!PtInRect(&rc, pt)) return DefWindowProc(hwnd, msg, wParam, lParam);
        const int cx = (rc.left + rc.right) / 2;
        const int cy = (rc.top + rc.bottom) / 2;
        const int R = (rc.right - rc.left) / 2;
        const long long dx = static_cast<long long>(pt.x - cx);
        const long long dy = static_cast<long long>(pt.y - cy);
        const long long dist2 = dx * dx + dy * dy;
        const long long rOuter = static_cast<long long>(R);
        const int innerR = std::max(1, R - FloaterDragRingPx());
        const long long rInner = static_cast<long long>(innerR);
        if (dist2 > rOuter * rOuter) return DefWindowProc(hwnd, msg, wParam, lParam);
        if (dist2 >= rInner * rInner) return HTCAPTION;
        return HTCLIENT;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        const HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc{};
        GetClientRect(hwnd, &rc);
        const int idx = static_cast<int>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        const bool isVisible = (idx >= 0 && idx < static_cast<int>(g_groups.size())) ? g_groups[static_cast<size_t>(idx)].g.visible : false;

        const COLORREF fillLo = isVisible ? kRgbGroupOnFill : kRgbGroupOffFill;
        const COLORREF fillHi =
            isVisible ? LuxBlendRgb(kRgbGroupOnFill, RGB(90, 210, 140), 0.22f) : LuxBlendRgb(kRgbGroupOffFill, RGB(255, 130, 140), 0.18f);
        if (HRGN clip = CreateEllipticRgn(rc.left, rc.top, rc.right, rc.bottom)) {
            SelectClipRgn(hdc, clip);
            LuxGradientVertical(hdc, rc, fillHi, fillLo);
            SelectClipRgn(hdc, nullptr);
            DeleteObject(clip);
        }

        if (g_penGlow && g_penFrame) {
            HBRUSH oldBr = static_cast<HBRUSH>(SelectObject(hdc, GetStockObject(NULL_BRUSH)));
            HPEN rim = CreatePen(PS_SOLID, 1, LuxBlendRgb(kRgbNeonDim, fillLo, 0.35f));
            HPEN oldP = static_cast<HPEN>(SelectObject(hdc, rim));
            Ellipse(hdc, rc.left, rc.top, rc.right, rc.bottom);
            SelectObject(hdc, g_penGlow);
            Ellipse(hdc, rc.left + 1, rc.top + 1, rc.right - 1, rc.bottom - 1);
            SelectObject(hdc, g_penFrame);
            Ellipse(hdc, rc.left + 2, rc.top + 2, rc.right - 2, rc.bottom - 2);
            SelectObject(hdc, oldP);
            SelectObject(hdc, oldBr);
            DeleteObject(rim);
        }

        std::string title = "Group";
        if (idx >= 0 && idx < static_cast<int>(g_groups.size())) title = g_groups[static_cast<size_t>(idx)].g.name;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, kRgbNeonCyan);
        HFONT oldF = nullptr;
        if (g_fontUi) oldF = static_cast<HFONT>(SelectObject(hdc, g_fontUi));
        RECT textRc = rc;
        InflateRect(&textRc, -12, -12);
        DrawTextA(hdc, title.c_str(), -1, &textRc, DT_CENTER | DT_VCENTER | DT_WORDBREAK | DT_END_ELLIPSIS | DT_NOPREFIX);
        RECT stateDot{ rc.right - 12, rc.top + 6, rc.right - 6, rc.top + 12 };
        HBRUSH stateBrush = CreateSolidBrush(isVisible ? RGB(30, 190, 110) : RGB(170, 60, 60));
        FillRect(hdc, &stateDot, stateBrush);
        DeleteObject(stateBrush);
        if (oldF) SelectObject(hdc, oldF);

        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONUP: {
        const int idx = static_cast<int>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        if (idx >= 0 && idx < static_cast<int>(g_groups.size())) {
            g_groups[static_cast<size_t>(idx)].g.visible = !g_groups[static_cast<size_t>(idx)].g.visible;
            RefreshGroupFloaterChrome(static_cast<size_t>(idx));
            if (g_groups[static_cast<size_t>(idx)].floater) PushGroupFloaterPulse(g_groups[static_cast<size_t>(idx)].floater);
            SyncFloaterVisibility();
            LayoutFloatingWidgets();
            if (g_nexusHwnd) {
                RefreshGroupList(g_nexusHwnd);
                RefreshWidgetList(g_nexusHwnd);
                WriteGroupToEditor(g_nexusHwnd, g_selectedGroupIndex);
            }
        }
        return 0;
    }
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_DISPLAYCHANGE) {
        LayoutFloatingWidgets();
        LayoutGroupFloaters();
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }

    if (msg == WM_APP_TRAY) {
        if (lParam == WM_LBUTTONDBLCLK) {
            ShowWindow(hWnd, SW_SHOW);
            SetForegroundWindow(hWnd);
            return 0;
        }
        if (lParam == WM_RBUTTONUP) {
            POINT pt{};
            GetCursorPos(&pt);
            HMENU m = CreatePopupMenu();
            constexpr UINT kTrayCmdShow = 1;
            constexpr UINT kTrayCmdExit = 2;
            constexpr UINT kTrayCmdAbout = 3;
            constexpr UINT kTrayCmdTheme = 4;
            constexpr UINT kTrayCmdDensity = 5;
            AppendMenuA(m, MF_STRING, kTrayCmdShow, "Show Nexus");
            AppendMenuA(m, MF_STRING, kTrayCmdTheme, "Cycle Theme");
            AppendMenuA(m, MF_STRING, kTrayCmdDensity, g_compactDensity ? "Density: Comfortable" : "Density: Compact");
            AppendMenuA(m, MF_STRING, kTrayCmdAbout, "About Widget Nexus…");
            AppendMenuA(m, MF_SEPARATOR, 0, nullptr);
            AppendMenuA(m, MF_STRING, kTrayCmdExit, "Exit");
            SetForegroundWindow(hWnd);
            const UINT cmd = TrackPopupMenu(m, TPM_RIGHTALIGN | TPM_BOTTOMALIGN | TPM_RETURNCMD, pt.x, pt.y, 0, hWnd, nullptr);
            DestroyMenu(m);
            if (cmd == kTrayCmdShow) {
                ShowWindow(hWnd, SW_SHOW);
                SetForegroundWindow(hWnd);
            } else if (cmd == kTrayCmdTheme) {
                CycleStylePreset();
                SetCompactDensity(g_compactDensity);
                ThemeCreate(GetLayoutDpi());
                LayoutNexusUi(hWnd, GetLayoutDpi());
                RebuildAllFloaters(g_hInstance);
                SetStatus(hWnd, std::string("Theme: ") + CurrentStylePresetName());
                InvalidateRect(hWnd, nullptr, FALSE);
            } else if (cmd == kTrayCmdDensity) {
                g_compactDensity = !g_compactDensity;
                SetCompactDensity(g_compactDensity);
                ThemeCreate(GetLayoutDpi());
                LayoutNexusUi(hWnd, GetLayoutDpi());
                RebuildAllFloaters(g_hInstance);
                SetStatus(hWnd, g_compactDensity ? "Density: Compact" : "Density: Comfortable");
                InvalidateRect(hWnd, nullptr, FALSE);
            } else if (cmd == kTrayCmdAbout) {
                ShowAboutWidgetNexus(hWnd);
            } else if (cmd == kTrayCmdExit) {
                DestroyWindow(hWnd);
            }
            return 0;
        }
        return 0;
    }

    if (msg == WM_APP_CMD_STATUS) {
        char* p = reinterpret_cast<char*>(lParam);
        if (p) {
            SetStatus(hWnd, std::string(p));
            delete[] p;
        }
        return 0;
    }

    if (msg == WM_APP_CMD_DONE) {
        if (wParam != 0) {
            SetStatus(hWnd, "[OK] Widget run finished.");
        } else {
            SetStatus(hWnd, "[ERR] Widget run failed.");
        }
        return 0;
    }

    switch (msg) {
    case WM_CREATE: {
        g_nexusHwnd = hWnd;
        const auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        g_hInstance = cs->hInstance;

        InitCommonControls();
        SetLayoutDpiFromWindow(hWnd);
        SetCompactDensity(g_compactDensity);
        ThemeCreate(GetLayoutDpi());

        constexpr int TOP = 58;
        constexpr int LEFT_X = 16;
        constexpr int LEFT_W = 336;
        constexpr int RIGHT_X = 372;
        constexpr int RIGHT_W = 572;
        constexpr int BTN_W = 76;
        constexpr int BTN_H = 34;
        constexpr int BTN_GAP = 6;

        HWND hShow = CreateWindowA("BUTTON", "Show non-pinned widget windows", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
            LEFT_X, TOP + 6, LEFT_W, 26, hWnd, reinterpret_cast<HMENU>(IDC_SHOW_NON_PINNED), nullptr, nullptr);
        ApplyNeonControlSkin(hShow);
        SendMessageA(hShow, BM_SETCHECK, BST_CHECKED, 0);

        HWND hList = CreateWindowA(
            "LISTBOX",
            "",
            WS_VISIBLE | WS_CHILD | WS_BORDER | LBS_NOTIFY | WS_VSCROLL | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT,
            LEFT_X,
            TOP + 36,
            LEFT_W,
            332,
            hWnd,
            reinterpret_cast<HMENU>(IDC_LIST_WIDGETS),
            nullptr,
            nullptr);
        ApplyNeonControlSkin(hList);
        SendMessageA(hList, LB_SETITEMHEIGHT, static_cast<WPARAM>(g_listItemHeight), 0);

        const int rowY = TOP + 378;
        const auto makeNeonBtn = [&](const char* text, int x, int id) {
            HWND b = CreateWindowA("BUTTON", text, WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
                x, rowY, BTN_W, BTN_H, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
            SetWindowTheme(b, L"", L"");
            return b;
        };
        makeNeonBtn("Run", LEFT_X, IDC_BTN_RUN);
        makeNeonBtn("Add", LEFT_X + (BTN_W + BTN_GAP), IDC_BTN_ADD);
        makeNeonBtn("Delete", LEFT_X + 2 * (BTN_W + BTN_GAP), IDC_BTN_DELETE);
        makeNeonBtn("Save", LEFT_X + 3 * (BTN_W + BTN_GAP), IDC_BTN_SAVE);
        makeNeonBtn("Hide", LEFT_X + 4 * (BTN_W + BTN_GAP), IDC_BTN_HIDE_NEXUS);

        HWND hLblName = CreateWindowA("STATIC", "Name:", WS_VISIBLE | WS_CHILD,
            RIGHT_X, TOP + 4, 60, 22, hWnd, reinterpret_cast<HMENU>(IDC_LBL_NAME), nullptr, nullptr);
        ApplyNeonControlSkin(hLblName);
        HWND hName = CreateWindowA("EDIT", "", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
            RIGHT_X + 60, TOP + 2, 188, 28, hWnd, reinterpret_cast<HMENU>(IDC_EDIT_NAME), nullptr, nullptr);
        ApplyNeonControlSkin(hName);

        HWND hLblGroup = CreateWindowA("STATIC", "Group:", WS_VISIBLE | WS_CHILD,
            RIGHT_X + 256, TOP + 4, 52, 22, hWnd, reinterpret_cast<HMENU>(IDC_LBL_GROUP), nullptr, nullptr);
        ApplyNeonControlSkin(hLblGroup);
        HWND hGroup = CreateWindowA("COMBOBOX", "", WS_VISIBLE | WS_CHILD | WS_BORDER | CBS_DROPDOWNLIST,
            RIGHT_X + 308, TOP + 2, 256, 380, hWnd, reinterpret_cast<HMENU>(IDC_COMBO_GROUP), nullptr, nullptr);
        ApplyNeonControlSkin(hGroup);

        HWND hAlways = CreateWindowA("BUTTON", "Always visible (pinned)", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
            RIGHT_X + 60, TOP + 36, 240, 24, hWnd, reinterpret_cast<HMENU>(IDC_CHECK_ALWAYS), nullptr, nullptr);
        ApplyNeonControlSkin(hAlways);

        HWND hLblCmd = CreateWindowA("STATIC", "Commands (one per line):", WS_VISIBLE | WS_CHILD,
            RIGHT_X, TOP + 72, 210, 22, hWnd, reinterpret_cast<HMENU>(IDC_LBL_COMMANDS), nullptr, nullptr);
        ApplyNeonControlSkin(hLblCmd);
        HWND hCmd = CreateWindowA("EDIT", "", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_LEFT | ES_MULTILINE | WS_VSCROLL | ES_AUTOVSCROLL,
            RIGHT_X, TOP + 96, 430, 282, hWnd, reinterpret_cast<HMENU>(IDC_EDIT_COMMANDS), nullptr, nullptr);
        ApplyNeonControlSkin(hCmd);
        SendMessageA(hCmd, WM_SETFONT, reinterpret_cast<WPARAM>(g_fontMono), TRUE);
        CreateWindowA("BUTTON", "Add Cmd", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
            RIGHT_X, TOP + 382, 64, 28, hWnd, reinterpret_cast<HMENU>(IDC_BTN_CMD_ADD), nullptr, nullptr);
        CreateWindowA("BUTTON", "Del Cmd", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
            RIGHT_X + 70, TOP + 382, 64, 28, hWnd, reinterpret_cast<HMENU>(IDC_BTN_CMD_DEL), nullptr, nullptr);
        CreateWindowA("BUTTON", "Up", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
            RIGHT_X + 140, TOP + 382, 64, 28, hWnd, reinterpret_cast<HMENU>(IDC_BTN_CMD_UP), nullptr, nullptr);
        CreateWindowA("BUTTON", "Down", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
            RIGHT_X + 210, TOP + 382, 64, 28, hWnd, reinterpret_cast<HMENU>(IDC_BTN_CMD_DOWN), nullptr, nullptr);
        CreateWindowA("BUTTON", "Run Line", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
            RIGHT_X + 280, TOP + 382, 150, 28, hWnd, reinterpret_cast<HMENU>(IDC_BTN_CMD_RUN), nullptr, nullptr);
        SetWindowTheme(GetDlgItem(hWnd, IDC_BTN_CMD_ADD), L"", L"");
        SetWindowTheme(GetDlgItem(hWnd, IDC_BTN_CMD_DEL), L"", L"");
        SetWindowTheme(GetDlgItem(hWnd, IDC_BTN_CMD_UP), L"", L"");
        SetWindowTheme(GetDlgItem(hWnd, IDC_BTN_CMD_DOWN), L"", L"");
        SetWindowTheme(GetDlgItem(hWnd, IDC_BTN_CMD_RUN), L"", L"");

        HWND hLblGroups = CreateWindowA("STATIC", "Groups:", WS_VISIBLE | WS_CHILD,
            RIGHT_X + 438, TOP + 72, 120, 22, hWnd, reinterpret_cast<HMENU>(IDC_LBL_GROUPS), nullptr, nullptr);
        ApplyNeonControlSkin(hLblGroups);
        HWND hGroupList = CreateWindowA("LISTBOX", "", WS_VISIBLE | WS_CHILD | WS_BORDER | LBS_NOTIFY | WS_VSCROLL | LBS_HASSTRINGS,
            RIGHT_X + 438, TOP + 96, 126, 186, hWnd, reinterpret_cast<HMENU>(IDC_LIST_GROUPS), nullptr, nullptr);
        ApplyNeonControlSkin(hGroupList);
        HWND hGroupName = CreateWindowA("EDIT", "", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
            RIGHT_X + 438, TOP + 288, 126, 24, hWnd, reinterpret_cast<HMENU>(IDC_EDIT_GROUP_NAME), nullptr, nullptr);
        ApplyNeonControlSkin(hGroupName);
        HWND hGroupAlways = CreateWindowA("BUTTON", "Pin", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
            RIGHT_X + 438, TOP + 316, 126, 22, hWnd, reinterpret_cast<HMENU>(IDC_CHECK_GROUP_ALWAYS), nullptr, nullptr);
        ApplyNeonControlSkin(hGroupAlways);
        makeNeonBtn("Add G", RIGHT_X + 438, IDC_BTN_ADD_GROUP);
        SetWindowPos(GetDlgItem(hWnd, IDC_BTN_ADD_GROUP), nullptr, RIGHT_X + 438, TOP + 344, 60, 28, SWP_NOZORDER | SWP_NOACTIVATE);
        makeNeonBtn("Del G", RIGHT_X + 504, IDC_BTN_DELETE_GROUP);
        SetWindowPos(GetDlgItem(hWnd, IDC_BTN_DELETE_GROUP), nullptr, RIGHT_X + 504, TOP + 344, 60, 28, SWP_NOZORDER | SWP_NOACTIVATE);

        HWND hStatus = CreateWindowA("STATIC", "Ready.", WS_VISIBLE | WS_CHILD,
            20, TOP + 416, RIGHT_W + LEFT_W + 16, 24, hWnd, reinterpret_cast<HMENU>(IDC_STATIC_STATUS), nullptr, nullptr);
        ApplyNeonControlSkin(hStatus);

        LayoutNexusUi(hWnd, GetLayoutDpi());

        LoadWidgetsFromDisk(g_configPath);
        RebuildAllFloaters(g_hInstance);
        RefreshGroupList(hWnd);
        RefreshWidgetList(hWnd);
        RefreshGroupCombo(hWnd);
        SetStatus(hWnd, "Loaded widgets from widgets.txt");
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }

    case WM_DPICHANGED: {
        const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
        if (suggested) {
            SetWindowPos(
                hWnd,
                nullptr,
                suggested->left,
                suggested->top,
                suggested->right - suggested->left,
                suggested->bottom - suggested->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
        }
        SetLayoutDpiFromWindow(hWnd);
        ThemeDestroy();
        ThemeCreate(GetLayoutDpi());
        LayoutNexusUi(hWnd, GetLayoutDpi());
        InvalidateRect(hWnd, nullptr, FALSE);
        LayoutFloatingWidgets();
        return TRUE;
    }

    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) {
            LayoutNexusUi(hWnd, GetLayoutDpi());
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps{};
        const HDC hdc = BeginPaint(hWnd, &ps);
        PaintNeonWindow(hWnd, hdc);
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_CTLCOLOREDIT: {
        const HDC hdc = reinterpret_cast<HDC>(wParam);
        SetBkColor(hdc, kRgbEdit);
        SetTextColor(hdc, kRgbTextHi);
        return reinterpret_cast<LRESULT>(g_brEdit);
    }

    case WM_CTLCOLORLISTBOX: {
        const HDC hdc = reinterpret_cast<HDC>(wParam);
        SetBkColor(hdc, kRgbPanel);
        SetTextColor(hdc, kRgbTextHi);
        return reinterpret_cast<LRESULT>(g_brPanel);
    }

    case WM_CTLCOLORSTATIC: {
        const HDC hdc = reinterpret_cast<HDC>(wParam);
        const HWND ctl = reinterpret_cast<HWND>(lParam);
        SetBkMode(hdc, OPAQUE);
        if (ctl == GetDlgItem(hWnd, IDC_STATIC_STATUS) && g_brStatus) {
            SetBkColor(hdc, kRgbBgStatus);
            SetTextColor(hdc, LuxBlendRgb(kRgbTextHi, kRgbNeonCyan, 0.12f));
            return reinterpret_cast<LRESULT>(g_brStatus);
        }
        SetBkColor(hdc, kRgbPanel);
        SetTextColor(hdc, kRgbTextHi);
        return reinterpret_cast<LRESULT>(g_brPanel);
    }

    case WM_CTLCOLORBTN: {
        const HDC hdc = reinterpret_cast<HDC>(wParam);
        SetBkMode(hdc, OPAQUE);
        SetBkColor(hdc, kRgbPanel);
        SetTextColor(hdc, kRgbNeonCyan);
        return reinterpret_cast<LRESULT>(g_brPanel);
    }

    case WM_MEASUREITEM: {
        auto* mis = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
        if (mis && mis->CtlID == IDC_LIST_WIDGETS) {
            mis->itemHeight = static_cast<UINT>(g_listItemHeight);
            return TRUE;
        }
        break;
    }

    case WM_DRAWITEM: {
        const auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (!dis) break;
        if (dis->CtlType == ODT_BUTTON) {
            DrawOwnerDrawButton(*dis);
            return TRUE;
        }
        if (dis->CtlType == ODT_LISTBOX && dis->CtlID == IDC_LIST_WIDGETS) {
            DrawOwnerDrawListItem(*dis);
            return TRUE;
        }
        break;
    }

    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        const int code = HIWORD(wParam);
        if (code == BN_CLICKED) {
            const bool ownerDrawButton =
                id == IDC_BTN_RUN || id == IDC_BTN_ADD || id == IDC_BTN_DELETE || id == IDC_BTN_SAVE ||
                id == IDC_BTN_HIDE_NEXUS || id == IDC_BTN_ADD_GROUP || id == IDC_BTN_DELETE_GROUP ||
                id == IDC_BTN_CMD_ADD || id == IDC_BTN_CMD_DEL || id == IDC_BTN_CMD_UP ||
                id == IDC_BTN_CMD_DOWN || id == IDC_BTN_CMD_RUN;
            if (ownerDrawButton) {
                StartButtonPressAnim(GetDlgItem(hWnd, id));
            }
        }

        if (id == IDC_SHOW_NON_PINNED && code == BN_CLICKED) {
            SaveCurrentEditorWidget(hWnd);
            g_showNonPinned = (SendMessageA(GetDlgItem(hWnd, IDC_SHOW_NON_PINNED), BM_GETCHECK, 0, 0) == BST_CHECKED);
            SyncFloaterVisibility();
            LayoutFloatingWidgets();
            RefreshWidgetList(hWnd);
            return 0;
        }

        if (id == IDC_COMBO_GROUP && code == CBN_SELCHANGE) {
            SaveCurrentEditorWidget(hWnd);
            SyncFloaterVisibility();
            LayoutFloatingWidgets();
            RefreshWidgetList(hWnd);
            return 0;
        }

        if (id == IDC_CHECK_ALWAYS && code == BN_CLICKED) {
            ReadEditorToWidget(hWnd, g_selectedModelIndex);
            if (g_selectedModelIndex >= 0) {
                const size_t idx = static_cast<size_t>(g_selectedModelIndex);
                RefreshFloaterChrome(idx);
                SyncFloaterVisibility();
                LayoutFloatingWidgets();
            }
            RefreshWidgetList(hWnd);
            return 0;
        }

        if (id == IDC_CHECK_GROUP_ALWAYS && code == BN_CLICKED) {
            ReadEditorToGroup(hWnd, g_selectedGroupIndex);
            if (g_selectedGroupIndex >= 0) {
                const size_t idx = static_cast<size_t>(g_selectedGroupIndex);
                RefreshGroupFloaterChrome(idx);
            }
            RefreshGroupList(hWnd);
            RefreshWidgetList(hWnd);
            return 0;
        }

        if (id == IDC_BTN_HIDE_NEXUS && code == BN_CLICKED) {
            TrayAdd(hWnd);
            ShowWindow(hWnd, SW_HIDE);
            return 0;
        }

        if (id == IDC_LIST_WIDGETS && code == LBN_SELCHANGE) {
            const int prevSel = g_selectedModelIndex;
            HWND hList = GetDlgItem(hWnd, IDC_LIST_WIDGETS);
            const int sel = static_cast<int>(SendMessageA(hList, LB_GETCURSEL, 0, 0));
            if (prevSel >= 0 && prevSel < static_cast<int>(g_rows.size())) {
                ReadEditorToWidget(hWnd, prevSel);
                RefreshFloaterChrome(static_cast<size_t>(prevSel));
            }
            if (sel != LB_ERR) {
                g_selectedModelIndex = static_cast<int>(SendMessageA(hList, LB_GETITEMDATA, sel, 0));
                WriteWidgetToEditor(hWnd, g_selectedModelIndex);
                StartWidgetSelectionAnim(hList, g_selectedModelIndex);
            } else {
                g_selectedModelIndex = -1;
                WriteWidgetToEditor(hWnd, -1);
            }
            return 0;
        }

        if (id == IDC_LIST_GROUPS && code == LBN_SELCHANGE) {
            HWND hList = GetDlgItem(hWnd, IDC_LIST_GROUPS);
            const int sel = static_cast<int>(SendMessageA(hList, LB_GETCURSEL, 0, 0));
            if (g_selectedGroupIndex >= 0 && g_selectedGroupIndex < static_cast<int>(g_groups.size())) {
                ReadEditorToGroup(hWnd, g_selectedGroupIndex);
                RefreshGroupFloaterChrome(static_cast<size_t>(g_selectedGroupIndex));
            }
            if (sel != LB_ERR) {
                g_selectedGroupIndex = static_cast<int>(SendMessageA(hList, LB_GETITEMDATA, sel, 0));
                WriteGroupToEditor(hWnd, g_selectedGroupIndex);
            } else {
                g_selectedGroupIndex = -1;
                WriteGroupToEditor(hWnd, -1);
            }
            return 0;
        }

        if (id == IDC_BTN_ADD && code == BN_CLICKED) {
            SaveCurrentEditorWidget(hWnd);
            Widget w;
            w.name = "New Widget";
            w.alwaysVisible = false;
            w.groupName.clear();
            w.commands.push_back("echo hello");
            g_rows.push_back({ w, nullptr });
            g_selectedModelIndex = static_cast<int>(g_rows.size()) - 1;
            CreateFloaterForIndex(g_hInstance, static_cast<size_t>(g_selectedModelIndex));
            UpdateFloaterIndices();
            LayoutFloatingWidgets();
            SyncFloaterVisibility();
            ApplyFloaterTopmost(static_cast<size_t>(g_selectedModelIndex));
            RefreshWidgetList(hWnd);
            WriteWidgetToEditor(hWnd, g_selectedModelIndex);
            SetStatus(hWnd, "Widget added.");
            return 0;
        }

        if (id == IDC_BTN_ADD_GROUP && code == BN_CLICKED) {
            if (g_selectedGroupIndex >= 0 && g_selectedGroupIndex < static_cast<int>(g_groups.size())) {
                ReadEditorToGroup(hWnd, g_selectedGroupIndex);
                RefreshGroupFloaterChrome(static_cast<size_t>(g_selectedGroupIndex));
            }
            Group g;
            g.name = Trim(ReadEditText(GetDlgItem(hWnd, IDC_EDIT_GROUP_NAME)));
            if (g.name.empty()) g.name = "Group " + std::to_string(g_groups.size() + 1);
            if (FindGroupIndexByName(g.name) >= 0) {
                SetStatus(hWnd, "Group name already exists.");
                return 0;
            }
            g.alwaysVisible = (SendMessageA(GetDlgItem(hWnd, IDC_CHECK_GROUP_ALWAYS), BM_GETCHECK, 0, 0) == BST_CHECKED);
            g.visible = true;
            g_groups.push_back({ g, nullptr });
            g_selectedGroupIndex = static_cast<int>(g_groups.size()) - 1;
            CreateGroupFloaterForIndex(g_hInstance, static_cast<size_t>(g_selectedGroupIndex));
            UpdateGroupFloaterIndices();
            LayoutGroupFloaters();
            SyncFloaterVisibility();
            LayoutFloatingWidgets();
            ApplyGroupFloaterTopmost(static_cast<size_t>(g_selectedGroupIndex));
            RefreshGroupList(hWnd);
            WriteGroupToEditor(hWnd, g_selectedGroupIndex);
            SetStatus(hWnd, "Group added.");
            return 0;
        }

        if (id == IDC_BTN_DELETE && code == BN_CLICKED) {
            if (g_selectedModelIndex >= 0 && g_selectedModelIndex < static_cast<int>(g_rows.size())) {
                if (g_rows[static_cast<size_t>(g_selectedModelIndex)].floater) {
                    DestroyWindow(g_rows[static_cast<size_t>(g_selectedModelIndex)].floater);
                    g_rows[static_cast<size_t>(g_selectedModelIndex)].floater = nullptr;
                }
                g_rows.erase(g_rows.begin() + g_selectedModelIndex);
                g_selectedModelIndex = -1;
                UpdateFloaterIndices();
                LayoutFloatingWidgets();
                RefreshWidgetList(hWnd);
                SetStatus(hWnd, "Widget deleted.");
            }
            return 0;
        }

        if (id == IDC_BTN_DELETE_GROUP && code == BN_CLICKED) {
            if (g_selectedGroupIndex >= 0 && g_selectedGroupIndex < static_cast<int>(g_groups.size())) {
                const std::string name = g_groups[static_cast<size_t>(g_selectedGroupIndex)].g.name;
                if (g_groups[static_cast<size_t>(g_selectedGroupIndex)].floater) {
                    DestroyWindow(g_groups[static_cast<size_t>(g_selectedGroupIndex)].floater);
                    g_groups[static_cast<size_t>(g_selectedGroupIndex)].floater = nullptr;
                }
                g_groups.erase(g_groups.begin() + g_selectedGroupIndex);
                RemoveGroupAssignment(name);
                g_selectedGroupIndex = -1;
                UpdateGroupFloaterIndices();
                LayoutGroupFloaters();
                SyncFloaterVisibility();
                LayoutFloatingWidgets();
                RefreshGroupList(hWnd);
                RefreshWidgetList(hWnd);
                WriteGroupToEditor(hWnd, -1);
                if (g_selectedModelIndex >= 0) WriteWidgetToEditor(hWnd, g_selectedModelIndex);
                SetStatus(hWnd, "Group deleted.");
            }
            return 0;
        }

        if (id == IDC_BTN_SAVE && code == BN_CLICKED) {
            SaveCurrentEditorWidget(hWnd);
            if (g_selectedGroupIndex >= 0 && g_selectedGroupIndex < static_cast<int>(g_groups.size())) {
                ReadEditorToGroup(hWnd, g_selectedGroupIndex);
            }
            RefreshWidgetList(hWnd);
            RefreshGroupList(hWnd);
            if (SaveWidgetsToDisk(g_configPath)) SetStatus(hWnd, "[OK] Saved widgets.txt");
            else SetStatus(hWnd, "[ERR] Failed to save widgets.txt");
            return 0;
        }

        if (id == IDC_BTN_RUN && code == BN_CLICKED) {
            SaveCurrentEditorWidget(hWnd);
            RefreshWidgetList(hWnd);
            RequestRunWidget(hWnd, g_selectedModelIndex);
            return 0;
        }
        if (id == IDC_BTN_CMD_ADD && code == BN_CLICKED) {
            if (AddCommandLineAtSelection(hWnd)) SetStatus(hWnd, "Command line inserted.");
            return 0;
        }
        if (id == IDC_BTN_CMD_DEL && code == BN_CLICKED) {
            if (DeleteCommandLineAtSelection(hWnd)) SetStatus(hWnd, "Command line deleted.");
            return 0;
        }
        if (id == IDC_BTN_CMD_UP && code == BN_CLICKED) {
            if (MoveCommandLineAtSelection(hWnd, true)) SetStatus(hWnd, "Command moved up.");
            return 0;
        }
        if (id == IDC_BTN_CMD_DOWN && code == BN_CLICKED) {
            if (MoveCommandLineAtSelection(hWnd, false)) SetStatus(hWnd, "Command moved down.");
            return 0;
        }
        if (id == IDC_BTN_CMD_RUN && code == BN_CLICKED) {
            SaveCurrentEditorWidget(hWnd);
            int line = -1;
            const std::string cmd = GetSelectedCommandLine(hWnd, &line);
            if (!cmd.empty()) {
                RequestRunSingleCommand(hWnd, cmd, "Line " + std::to_string(line + 1));
            } else {
                SetStatus(hWnd, "No command line selected.");
            }
            return 0;
        }
        break;
    }

    case WM_TIMER:
        if (wParam == IDT_FLOATER_ANIM) {
            TickFloaterAnimations();
            return 0;
        }
        if (wParam == IDT_UI_ANIM) {
            TickUiAnimations();
            return 0;
        }
        break;

    case WM_DESTROY:
        TrayRemove();
        DestroyAllFloaters();
        g_buttonPressAnims.clear();
        g_widgetSelectAnim = ListSelectAnim{};
        KillTimer(hWnd, IDT_UI_ANIM);
        ThemeDestroy();
        if (g_trayIcon) {
            DestroyIcon(g_trayIcon);
            g_trayIcon = nullptr;
        }
        if (g_windowIcon) {
            DestroyIcon(g_windowIcon);
            g_windowIcon = nullptr;
        }
        if (g_windowIconSm) {
            DestroyIcon(g_windowIconSm);
            g_windowIconSm = nullptr;
        }
        g_nexusHwnd = nullptr;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    InitProcessDpi();
    g_hInstance = hInstance;
    InitConfigPathBesideExe(g_configPath);

    const char CLASS_NAME[] = "WidgetLauncherWin32";

    g_windowIcon = CreateNeonWlIcon(32);
    g_windowIconSm = CreateNeonWlIcon(16);

    WNDCLASSA wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
    RegisterClassA(&wc);

    WNDCLASSA wf{};
    wf.lpfnWndProc = FloaterWndProc;
    wf.hInstance = hInstance;
    wf.lpszClassName = FLOATER_CLASS_NAME;
    wf.hCursor = LoadCursor(nullptr, IDC_HAND);
    wf.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
    RegisterClassA(&wf);

    WNDCLASSA wg{};
    wg.lpfnWndProc = GroupFloaterWndProc;
    wg.hInstance = hInstance;
    wg.lpszClassName = GROUP_FLOATER_CLASS_NAME;
    wg.hCursor = LoadCursor(nullptr, IDC_HAND);
    wg.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
    RegisterClassA(&wg);

    UINT sysDpi = 96;
    using GetDpiForSystemFn = UINT(WINAPI*)();
    if (HMODULE u = GetModuleHandleA("user32.dll")) {
        if (auto* gdfs = reinterpret_cast<GetDpiForSystemFn>(GetProcAddress(u, "GetDpiForSystem"))) {
            sysDpi = gdfs();
        }
    }
    if (sysDpi == 96) {
        HDC sdc = GetDC(nullptr);
        if (sdc) {
            sysDpi = static_cast<UINT>(GetDeviceCaps(sdc, LOGPIXELSX));
            ReleaseDC(nullptr, sdc);
        }
    }
    const int winW = MulDiv(960, static_cast<int>(sysDpi), 96);
    const int winH = MulDiv(700, static_cast<int>(sysDpi), 96);

    HWND hWnd = CreateWindowExA(
        0, CLASS_NAME, "WIDGET NEXUS",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, winW, winH,
        nullptr, nullptr, hInstance, nullptr);

    if (!hWnd) {
        if (g_windowIcon) {
            DestroyIcon(g_windowIcon);
            g_windowIcon = nullptr;
        }
        if (g_windowIconSm) {
            DestroyIcon(g_windowIconSm);
            g_windowIconSm = nullptr;
        }
        return 0;
    }
    g_nexusHwnd = hWnd;
    if (g_windowIcon) SendMessageA(hWnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(g_windowIcon));
    if (g_windowIconSm) SendMessageA(hWnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(g_windowIconSm));
    ShowWindow(hWnd, nCmdShow);
    AnimateWindow(hWnd, 180, AW_BLEND);
    UpdateWindow(hWnd);

    MSG msg{};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
