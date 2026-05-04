#pragma once

#include <windows.h>

constexpr const char kAppVersion[] = "0.2.0";
constexpr const char kAppRepoUrl[] = "https://github.com/robbinc91/widget-nexus";

constexpr UINT WM_APP_TRAY = WM_USER + 101;
constexpr UINT WM_APP_CMD_STATUS = WM_APP + 220;
constexpr UINT WM_APP_CMD_DONE = WM_APP + 221;

constexpr const char FLOATER_CLASS_NAME[] = "NexusFloaterWnd";
constexpr const char GROUP_FLOATER_CLASS_NAME[] = "NexusGroupFloaterWnd";

constexpr int kFloaterDiameter96 = 76;
constexpr int kGroupFloaterDiameter96 = kFloaterDiameter96;
constexpr int kFloaterDragRingPx96 = 12;
constexpr int kFloaterGap96 = 8;
constexpr int kFloaterMargin96 = 10;
constexpr BYTE kFloaterAlpha = 200;

constexpr UINT_PTR IDT_FLOATER_ANIM = WM_USER + 150;
constexpr UINT_PTR IDT_UI_ANIM = WM_USER + 151;
constexpr UINT kFloaterAnimIntervalMs = 20;
constexpr UINT kUiAnimIntervalMs = 15;

enum class FloaterAnimKind : unsigned char { FadeIn, FadeOut, PulseDown, PulseUp };

struct FloaterAnim {
    HWND hwnd = nullptr;
    BYTE alpha = 0;
    FloaterAnimKind kind = FloaterAnimKind::FadeIn;
};

struct ButtonPressAnim {
    HWND hwnd = nullptr;
    unsigned int ticksLeft = 0;
    unsigned int ticksTotal = 0;
};

struct ListSelectAnim {
    HWND listHwnd = nullptr;
    int selectedItemData = -1;
    unsigned int ticksLeft = 0;
    unsigned int ticksTotal = 0;
};

enum ControlIds {
    IDC_SHOW_NON_PINNED = 101,
    IDC_LIST_WIDGETS = 102,
    IDC_BTN_RUN = 103,
    IDC_BTN_ADD = 104,
    IDC_BTN_DELETE = 105,
    IDC_BTN_SAVE = 106,
    IDC_EDIT_NAME = 107,
    IDC_CHECK_ALWAYS = 108,
    IDC_EDIT_COMMANDS = 109,
    IDC_STATIC_STATUS = 110,
    IDC_BTN_HIDE_NEXUS = 111,
    IDC_COMBO_GROUP = 112,
    IDC_LIST_GROUPS = 113,
    IDC_EDIT_GROUP_NAME = 114,
    IDC_BTN_ADD_GROUP = 115,
    IDC_BTN_DELETE_GROUP = 116,
    IDC_CHECK_GROUP_ALWAYS = 117,
    IDC_BTN_CMD_ADD = 118,
    IDC_BTN_CMD_DEL = 119,
    IDC_BTN_CMD_UP = 120,
    IDC_BTN_CMD_DOWN = 121,
    IDC_BTN_CMD_RUN = 122,
    IDC_LBL_NAME = 123,
    IDC_LBL_GROUP = 124,
    IDC_LBL_COMMANDS = 125,
    IDC_LBL_GROUPS = 126
};
