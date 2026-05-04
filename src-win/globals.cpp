#include "globals.hpp"

std::vector<WidgetRow> g_rows;
std::vector<GroupRow> g_groups;
bool g_showNonPinned = true;
bool g_compactDensity = false;
int g_selectedModelIndex = -1;
int g_selectedGroupIndex = -1;
std::string g_configPath = "widgets.txt";

HINSTANCE g_hInstance = nullptr;
HWND g_nexusHwnd = nullptr;
bool g_trayIconAdded = false;
HICON g_trayIcon = nullptr;
HICON g_windowIcon = nullptr;
HICON g_windowIconSm = nullptr;

std::vector<FloaterAnim> g_floaterAnims;
std::vector<ButtonPressAnim> g_buttonPressAnims;
ListSelectAnim g_widgetSelectAnim;

HBRUSH g_brDeep = nullptr;
HBRUSH g_brPanel = nullptr;
HBRUSH g_brEdit = nullptr;
HBRUSH g_brListSel = nullptr;
HBRUSH g_brBtn = nullptr;
HBRUSH g_brBtnHot = nullptr;
HPEN g_penFrame = nullptr;
HPEN g_penGlow = nullptr;
HFONT g_fontTitle = nullptr;
HFONT g_fontUi = nullptr;
HFONT g_fontMono = nullptr;
HFONT g_fontSection = nullptr;
HBRUSH g_brStatus = nullptr;
int g_listItemHeight = 40;
