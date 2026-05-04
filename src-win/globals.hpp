#pragma once

#include "app_ids.hpp"
#include "model.hpp"

#include <string>
#include <vector>

struct WidgetRow {
    Widget w;
    HWND floater = nullptr;
};

struct GroupRow {
    Group g;
    HWND floater = nullptr;
};

extern std::vector<WidgetRow> g_rows;
extern std::vector<GroupRow> g_groups;
extern bool g_showNonPinned;
extern bool g_compactDensity;
extern int g_selectedModelIndex;
extern int g_selectedGroupIndex;
extern std::string g_configPath;

extern HINSTANCE g_hInstance;
extern HWND g_nexusHwnd;
extern bool g_trayIconAdded;
extern HICON g_trayIcon;
extern HICON g_windowIcon;
extern HICON g_windowIconSm;

extern std::vector<FloaterAnim> g_floaterAnims;
extern std::vector<ButtonPressAnim> g_buttonPressAnims;
extern ListSelectAnim g_widgetSelectAnim;

extern HBRUSH g_brDeep;
extern HBRUSH g_brPanel;
extern HBRUSH g_brEdit;
extern HBRUSH g_brListSel;
extern HBRUSH g_brBtn;
extern HBRUSH g_brBtnHot;
extern HPEN g_penFrame;
extern HPEN g_penGlow;
extern HFONT g_fontTitle;
extern HFONT g_fontUi;
extern HFONT g_fontMono;
extern HFONT g_fontSection;
extern HBRUSH g_brStatus;
extern int g_listItemHeight;
