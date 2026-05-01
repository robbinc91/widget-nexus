#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <uxtheme.h>

#include "motion_tokens.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

struct Widget {
    std::string name;
    bool alwaysVisible = false;
    std::string groupName;
    std::vector<std::string> commands;
};

struct WidgetRow {
    Widget w;
    HWND floater = nullptr;
};

struct Group {
    std::string name;
    bool alwaysVisible = false;
    bool visible = true;
};

struct GroupRow {
    Group g;
    HWND floater = nullptr;
};

static std::vector<WidgetRow> g_rows;
static std::vector<GroupRow> g_groups;
static bool g_showNonPinned = true;
static int g_selectedModelIndex = -1;
static int g_selectedGroupIndex = -1;
static std::string g_configPath = "widgets.txt";

static void InitConfigPathBesideExe() {
    char exe[MAX_PATH]{};
    const DWORD n = GetModuleFileNameA(nullptr, exe, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return;
    std::string path(exe, exe + static_cast<size_t>(n));
    const size_t slash = path.find_last_of("\\/");
    if (slash == std::string::npos) return;
    g_configPath = path.substr(0, slash + 1) + "widgets.txt";
}
static HINSTANCE g_hInstance = nullptr;
static HWND g_nexusHwnd = nullptr;
static bool g_trayIconAdded = false;
static HICON g_trayIcon = nullptr;
static HICON g_windowIcon = nullptr;
static HICON g_windowIconSm = nullptr;

static constexpr const char kAppVersion[] = "0.2.0";
static constexpr const char kAppRepoUrl[] = "https://github.com/robbinc91/widget-nexus";

static constexpr UINT WM_APP_TRAY = WM_USER + 101;
static constexpr const char FLOATER_CLASS_NAME[] = "NexusFloaterWnd";
static constexpr const char GROUP_FLOATER_CLASS_NAME[] = "NexusGroupFloaterWnd";

// Circular floater + matrix layout (top-right: column fills top→bottom, columns progress right→left)
static constexpr int kFloaterDiameter = 76;
static constexpr int kGroupFloaterDiameter = kFloaterDiameter;
static constexpr int kFloaterDragRingPx = 12;
static constexpr int kFloaterGap = 8;
static constexpr int kFloaterMargin = 10;
static constexpr BYTE kFloaterAlpha = 200; // layered window opacity (0 = invisible, 255 = opaque)

static constexpr UINT_PTR IDT_FLOATER_ANIM = WM_USER + 150;
static constexpr UINT_PTR IDT_UI_ANIM = WM_USER + 151;
static constexpr UINT kFloaterAnimIntervalMs = 20;
static constexpr BYTE kFloaterAnimStep = motion::AlphaStep(
    0,
    kFloaterAlpha,
    motion::DurationToTicks(motion::Tokens::kPanelMs, kFloaterAnimIntervalMs));
static constexpr BYTE kGroupPulseLowAlpha = static_cast<BYTE>((static_cast<unsigned int>(kFloaterAlpha) * 62u) / 100u);
static constexpr UINT kUiAnimIntervalMs = 15;

enum class FloaterAnimKind : unsigned char { FadeIn, FadeOut, PulseDown, PulseUp };

struct FloaterAnim {
    HWND hwnd = nullptr;
    BYTE alpha = 0;
    FloaterAnimKind kind = FloaterAnimKind::FadeIn;
};

static std::vector<FloaterAnim> g_floaterAnims;

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

static std::vector<ButtonPressAnim> g_buttonPressAnims;
static ListSelectAnim g_widgetSelectAnim;

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
    IDC_CHECK_GROUP_ALWAYS = 117
};

// Style spec v2 — see STYLE_SPEC_V2.md (surfaces, accents, text)
static constexpr COLORREF kRgbDeep0 = RGB(8, 10, 20);          // bg.canvas
static constexpr COLORREF kRgbPanel = RGB(16, 20, 42);        // bg.panel
static constexpr COLORREF kRgbEdit = RGB(12, 16, 34);         // bg.control
static constexpr COLORREF kRgbControlHover = RGB(18, 24, 48); // bg.controlHover
static constexpr COLORREF kRgbControlActive = RGB(24, 34, 66); // bg.controlActive
static constexpr COLORREF kRgbNeonCyan = RGB(0, 220, 235);    // accent.primary
static constexpr COLORREF kRgbNeonMagenta = RGB(235, 72, 198);  // accent.secondary
static constexpr COLORREF kRgbNeonDim = RGB(0, 128, 158);     // accent.primaryDim
static constexpr COLORREF kRgbTextHi = RGB(224, 246, 255);    // text.primary
static constexpr COLORREF kRgbTextLo = RGB(154, 188, 208);    // text.secondary
static constexpr COLORREF kRgbGroupOnFill = RGB(18, 48, 30);  // darkened state.success
static constexpr COLORREF kRgbGroupOffFill = RGB(48, 22, 26); // darkened state.error

// Luxury neon layer — depth, gradients (GradientFill / msimg32)
static constexpr COLORREF kRgbLuxCanvasTop = RGB(18, 22, 46);
static constexpr COLORREF kRgbLuxCanvasBot = RGB(6, 8, 17);
static constexpr COLORREF kRgbLuxPanelTop = RGB(28, 34, 62);
static constexpr COLORREF kRgbLuxShadow = RGB(3, 4, 12);
static constexpr COLORREF kRgbLuxVignetteEdge = RGB(4, 5, 14);
static constexpr COLORREF kRgbBgStatus = RGB(10, 14, 30);

static HBRUSH g_brDeep = nullptr;
static HBRUSH g_brPanel = nullptr;
static HBRUSH g_brEdit = nullptr;
static HBRUSH g_brListSel = nullptr;
static HBRUSH g_brBtn = nullptr;
static HBRUSH g_brBtnHot = nullptr;
static HPEN g_penFrame = nullptr;
static HPEN g_penGlow = nullptr;
static HFONT g_fontTitle = nullptr;
static HFONT g_fontUi = nullptr;
static HFONT g_fontMono = nullptr;
static HFONT g_fontSection = nullptr;
static HBRUSH g_brStatus = nullptr;
static int g_listItemHeight = 40;

static COLORREF LuxBlendRgb(COLORREF a, COLORREF b, float t) {
    t = std::max(0.0f, std::min(1.0f, t));
    const BYTE r = static_cast<BYTE>(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * t);
    const BYTE gg = static_cast<BYTE>(GetGValue(a) + (GetGValue(b) - GetGValue(a)) * t);
    const BYTE bb = static_cast<BYTE>(GetBValue(a) + (GetBValue(b) - GetBValue(a)) * t);
    return RGB(r, gg, bb);
}

static void LuxGradientVertical(HDC hdc, const RECT& rc, COLORREF top, COLORREF bottom) {
    TRIVERTEX tv[2]{};
    GRADIENT_RECT gr{};
    tv[0].x = rc.left;
    tv[0].y = rc.top;
    tv[0].Red = static_cast<COLOR16>(static_cast<USHORT>(GetRValue(top)) << 8);
    tv[0].Green = static_cast<COLOR16>(static_cast<USHORT>(GetGValue(top)) << 8);
    tv[0].Blue = static_cast<COLOR16>(static_cast<USHORT>(GetBValue(top)) << 8);
    tv[0].Alpha = 0xff00;
    tv[1].x = rc.right;
    tv[1].y = rc.bottom;
    tv[1].Red = static_cast<COLOR16>(static_cast<USHORT>(GetRValue(bottom)) << 8);
    tv[1].Green = static_cast<COLOR16>(static_cast<USHORT>(GetGValue(bottom)) << 8);
    tv[1].Blue = static_cast<COLOR16>(static_cast<USHORT>(GetBValue(bottom)) << 8);
    tv[1].Alpha = 0xff00;
    gr.UpperLeft = 0;
    gr.LowerRight = 1;
    GradientFill(hdc, tv, 2, &gr, 1, GRADIENT_FILL_RECT_V);
}

static void LuxGradientHorizontal(HDC hdc, const RECT& rc, COLORREF left, COLORREF rightC) {
    TRIVERTEX tv[2]{};
    GRADIENT_RECT gr{};
    tv[0].x = rc.left;
    tv[0].y = rc.top;
    tv[0].Red = static_cast<COLOR16>(static_cast<USHORT>(GetRValue(left)) << 8);
    tv[0].Green = static_cast<COLOR16>(static_cast<USHORT>(GetGValue(left)) << 8);
    tv[0].Blue = static_cast<COLOR16>(static_cast<USHORT>(GetBValue(left)) << 8);
    tv[0].Alpha = 0xff00;
    tv[1].x = rc.right;
    tv[1].y = rc.bottom;
    tv[1].Red = static_cast<COLOR16>(static_cast<USHORT>(GetRValue(rightC)) << 8);
    tv[1].Green = static_cast<COLOR16>(static_cast<USHORT>(GetGValue(rightC)) << 8);
    tv[1].Blue = static_cast<COLOR16>(static_cast<USHORT>(GetBValue(rightC)) << 8);
    tv[1].Alpha = 0xff00;
    gr.UpperLeft = 0;
    gr.LowerRight = 1;
    GradientFill(hdc, tv, 2, &gr, 1, GRADIENT_FILL_RECT_H);
}

static void PaintLuxuryPanelCard(HDC hdc, const RECT& panelRc) {
    RECT shadow = panelRc;
    OffsetRect(&shadow, 5, 5);
    HBRUSH sh = CreateSolidBrush(kRgbLuxShadow);
    FillRect(hdc, &shadow, sh);
    DeleteObject(sh);

    LuxGradientVertical(hdc, panelRc, kRgbLuxPanelTop, kRgbPanel);

    HPEN edge = CreatePen(PS_SOLID, 1, LuxBlendRgb(kRgbNeonDim, kRgbDeep0, 0.25f));
    HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, edge));
    HBRUSH oldBr = static_cast<HBRUSH>(SelectObject(hdc, GetStockObject(NULL_BRUSH)));
    Rectangle(hdc, panelRc.left, panelRc.top, panelRc.right, panelRc.bottom);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBr);
    DeleteObject(edge);

    HPEN hi = CreatePen(PS_SOLID, 1, LuxBlendRgb(kRgbNeonDim, kRgbNeonCyan, 0.4f));
    oldPen = static_cast<HPEN>(SelectObject(hdc, hi));
    MoveToEx(hdc, panelRc.left + 1, panelRc.top + 1, nullptr);
    LineTo(hdc, panelRc.right - 2, panelRc.top + 1);
    SelectObject(hdc, oldPen);
    DeleteObject(hi);
}

static void DrawLuxurySectionCaption(HDC hdc, const RECT& panelRc, const char* text, int baselineY) {
    if (!g_fontSection || !text) return;
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, LuxBlendRgb(kRgbTextLo, kRgbNeonCyan, 0.35f));
    HFONT oldF = static_cast<HFONT>(SelectObject(hdc, g_fontSection));
    RECT tr{ panelRc.left + 14, baselineY, panelRc.right - 12, baselineY + 18 };
    DrawTextA(hdc, text, -1, &tr, DT_LEFT | DT_SINGLELINE | DT_BOTTOM | DT_NOPREFIX);

    HPEN capLine = CreatePen(PS_SOLID, 1, LuxBlendRgb(kRgbNeonDim, kRgbNeonMagenta, 0.45f));
    HPEN oldP = static_cast<HPEN>(SelectObject(hdc, capLine));
    MoveToEx(hdc, tr.left, tr.bottom + 2, nullptr);
    LineTo(hdc, tr.left + 72, tr.bottom + 2);
    SelectObject(hdc, oldP);
    DeleteObject(capLine);

    SelectObject(hdc, oldF);
}

static HICON CreateNeonWlIcon(int pixelSize) {
    const int s = std::max(8, pixelSize);
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP color = CreateCompatibleBitmap(screen, s, s);
    HBITMAP oldBmp = static_cast<HBITMAP>(SelectObject(mem, color));

    RECT rc{ 0, 0, s, s };
    HBRUSH bg = CreateSolidBrush(RGB(150, 20, 160));
    FillRect(mem, &rc, bg);
    DeleteObject(bg);

    const int corner = std::max(2, (s * 6) / 16);
    HPEN neon = CreatePen(PS_SOLID, std::max(1, s / 16), RGB(255, 80, 240));
    HPEN oldP = static_cast<HPEN>(SelectObject(mem, neon));
    HBRUSH noFill = static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
    HBRUSH oldBr = static_cast<HBRUSH>(SelectObject(mem, noFill));
    RoundRect(mem, 0, 0, s, s, corner, corner);
    SelectObject(mem, oldP);
    SelectObject(mem, oldBr);
    DeleteObject(neon);

    const int fontH = std::max(8, (s * 10) / 16);
    HFONT f = CreateFontA(fontH, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
    HFONT oldF = static_cast<HFONT>(SelectObject(mem, f));
    SetBkMode(mem, TRANSPARENT);
    SetTextColor(mem, RGB(255, 210, 255));
    DrawTextA(mem, "WL", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(mem, oldF);
    DeleteObject(f);

    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.hbmColor = color;
    ii.hbmMask = CreateBitmap(s, s, 1, 1, nullptr);
    HICON icon = CreateIconIndirect(&ii);

    DeleteObject(ii.hbmMask);
    SelectObject(mem, oldBmp);
    DeleteObject(color);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    return icon;
}

static void ShowAboutWidgetNexus(HWND owner) {
    std::string text = "Widget Nexus — desktop widget launcher.\r\n\r\nVersion ";
    text += kAppVersion;
    text += "\r\n\r\nRepository:\r\n";
    text += kAppRepoUrl;
    text += "\r\n\r\nOpen the repository page in your browser now?";
    if (MessageBoxA(owner, text.c_str(), "About Widget Nexus", MB_YESNO | MB_ICONINFORMATION) == IDYES) {
        ShellExecuteA(owner, "open", kAppRepoUrl, nullptr, nullptr, SW_SHOWNORMAL);
    }
}

static void ThemeCreate() {
    if (g_brDeep) return;
    g_brDeep = CreateSolidBrush(kRgbDeep0);
    g_brPanel = CreateSolidBrush(kRgbPanel);
    g_brEdit = CreateSolidBrush(kRgbEdit);
    g_brListSel = CreateSolidBrush(kRgbControlActive);
    g_brBtn = CreateSolidBrush(kRgbEdit);
    g_brBtnHot = CreateSolidBrush(kRgbControlHover);
    g_penFrame = CreatePen(PS_SOLID, 1, kRgbNeonCyan);
    g_penGlow = CreatePen(PS_SOLID, 1, kRgbNeonDim);
    g_fontTitle = CreateFontA(24, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
    g_fontUi = CreateFontA(15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
    g_fontMono = CreateFontA(14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
    g_fontSection = CreateFontA(12, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
    g_brStatus = CreateSolidBrush(kRgbBgStatus);
}

static void ThemeDestroy() {
    if (g_fontTitle) { DeleteObject(g_fontTitle); g_fontTitle = nullptr; }
    if (g_fontUi) { DeleteObject(g_fontUi); g_fontUi = nullptr; }
    if (g_fontMono) { DeleteObject(g_fontMono); g_fontMono = nullptr; }
    if (g_fontSection) { DeleteObject(g_fontSection); g_fontSection = nullptr; }
    if (g_brStatus) { DeleteObject(g_brStatus); g_brStatus = nullptr; }
    if (g_penFrame) { DeleteObject(g_penFrame); g_penFrame = nullptr; }
    if (g_penGlow) { DeleteObject(g_penGlow); g_penGlow = nullptr; }
    if (g_brDeep) { DeleteObject(g_brDeep); g_brDeep = nullptr; }
    if (g_brPanel) { DeleteObject(g_brPanel); g_brPanel = nullptr; }
    if (g_brEdit) { DeleteObject(g_brEdit); g_brEdit = nullptr; }
    if (g_brListSel) { DeleteObject(g_brListSel); g_brListSel = nullptr; }
    if (g_brBtn) { DeleteObject(g_brBtn); g_brBtn = nullptr; }
    if (g_brBtnHot) { DeleteObject(g_brBtnHot); g_brBtnHot = nullptr; }
}

static void ApplyNeonControlSkin(HWND hCtrl) {
    if (!hCtrl) return;
    SetWindowTheme(hCtrl, L"", L"");
    SendMessageA(hCtrl, WM_SETFONT, reinterpret_cast<WPARAM>(g_fontUi), TRUE);
}

static void PaintNeonWindow(HWND hWnd, HDC hdc) {
    RECT rc{};
    GetClientRect(hWnd, &rc);
    LuxGradientVertical(hdc, rc, kRgbLuxCanvasTop, kRgbLuxCanvasBot);

    // Edge vignette (horizontal bands — cheap depth)
    const int canvasW = static_cast<int>(rc.right - rc.left);
    const int vignetteW = std::min(72, canvasW / 6);
    if (vignetteW > 8) {
        RECT vl{ rc.left, rc.top, rc.left + vignetteW, rc.bottom };
        LuxGradientHorizontal(hdc, vl, kRgbLuxVignetteEdge, LuxBlendRgb(kRgbLuxCanvasTop, kRgbLuxCanvasBot, 0.5f));
        RECT vr{ rc.right - vignetteW, rc.top, rc.right, rc.bottom };
        LuxGradientHorizontal(hdc, vr, LuxBlendRgb(kRgbLuxCanvasTop, kRgbLuxCanvasBot, 0.5f), kRgbLuxVignetteEdge);
    }

    // Scanline-style tint in header only (keeps repaint cheap)
    const int savedDc = SaveDC(hdc);
    IntersectClipRect(hdc, rc.left, rc.top, rc.right, 58);
    const int clipH = 58;
    for (int y = 0; y < clipH; y += 3) {
        const int t = (y * 255) / std::max(1, clipH);
        const COLORREF c = RGB(14 + t / 24, 18 + t / 28, 36 + t / 10);
        RECT strip{ rc.left, y, rc.right, y + 3 };
        HBRUSH b = CreateSolidBrush(c);
        FillRect(hdc, &strip, b);
        DeleteObject(b);
    }
    if (savedDc) RestoreDC(hdc, savedDc);

    // Luxury panel cards (drop shadow + vertical gradient + rim highlight)
    RECT leftPanel{ 8, 74, 360, rc.bottom - 44 };
    RECT rightPanel{ 364, 74, rc.right - 8, rc.bottom - 44 };
    PaintLuxuryPanelCard(hdc, leftPanel);
    PaintLuxuryPanelCard(hdc, rightPanel);

    constexpr int kGroupsColumnX = 372 + 438; // matches WM_CREATE layout
    DrawLuxurySectionCaption(hdc, leftPanel, "WIDGETS", leftPanel.top + 6);
    const int detailsRight = std::min(kGroupsColumnX - 6, static_cast<int>(rightPanel.right));
    RECT detailsBand{ rightPanel.left, rightPanel.top, detailsRight, rightPanel.bottom };
    DrawLuxurySectionCaption(hdc, detailsBand, "DETAILS", rightPanel.top + 6);
    const int groupsLo = rightPanel.left + 12;
    const int groupsHi = std::max(groupsLo + 44, static_cast<int>(rightPanel.right) - 40);
    const int groupsLeft = std::clamp(kGroupsColumnX - 8, groupsLo, groupsHi);
    RECT groupsBand{ groupsLeft, rightPanel.top, rightPanel.right, rightPanel.bottom };
    if (groupsBand.left < groupsBand.right - 40) {
        DrawLuxurySectionCaption(hdc, groupsBand, "GROUPS", rightPanel.top + 6);
    }

    // Bottom status strip (visual anchor; status control uses g_brStatus via WM_CTLCOLORSTATIC)
    RECT statusStrip{ rc.left + 8, rc.bottom - 40, rc.right - 8, rc.bottom - 8 };
    FillRect(hdc, &statusStrip, g_brStatus ? g_brStatus : g_brDeep);
    HPEN statusHi = CreatePen(PS_SOLID, 1, LuxBlendRgb(kRgbNeonDim, kRgbDeep0, 0.5f));
    HPEN oldStat = static_cast<HPEN>(SelectObject(hdc, statusHi));
    MoveToEx(hdc, statusStrip.left, statusStrip.top, nullptr);
    LineTo(hdc, statusStrip.right, statusStrip.top);
    SelectObject(hdc, oldStat);
    DeleteObject(statusHi);

    // Soft outer frame (1px dim) + inner accent along header only (v2)
    HPEN outerPen = CreatePen(PS_SOLID, 1, kRgbNeonDim);
    HPEN penRestore = static_cast<HPEN>(SelectObject(hdc, outerPen));
    HBRUSH oldBrFrame = static_cast<HBRUSH>(SelectObject(hdc, GetStockObject(NULL_BRUSH)));
    Rectangle(hdc, rc.left + 8, rc.top + 8, rc.right - 8, rc.bottom - 8);
    HPEN innerGlow = CreatePen(PS_SOLID, 1, LuxBlendRgb(kRgbNeonCyan, kRgbDeep0, 0.55f));
    HPEN penOuterHeld = static_cast<HPEN>(SelectObject(hdc, innerGlow));
    MoveToEx(hdc, 18, rc.top + 9, nullptr);
    LineTo(hdc, rc.right - 18, rc.top + 9);
    SelectObject(hdc, penOuterHeld);
    DeleteObject(innerGlow);
    SelectObject(hdc, penRestore);
    SelectObject(hdc, oldBrFrame);
    DeleteObject(outerPen);

    // Accent line under title zone (lighter secondary stroke)
    HPEN mag = CreatePen(PS_SOLID, 1, kRgbNeonMagenta);
    HPEN prevForMag = static_cast<HPEN>(SelectObject(hdc, mag));
    MoveToEx(hdc, 18, 52, nullptr);
    LineTo(hdc, rc.right - 18, 52);
    SelectObject(hdc, prevForMag);
    DeleteObject(mag);

    SetBkMode(hdc, TRANSPARENT);
    HFONT oldF = static_cast<HFONT>(SelectObject(hdc, g_fontTitle));
    RECT titleRc{ 18, 10, rc.right - 18, 52 };
    SetTextColor(hdc, RGB(0, 72, 88));
    RECT shadow = titleRc;
    OffsetRect(&shadow, 2, 2);
    DrawTextA(hdc, "WIDGET NEXUS", -1, &shadow, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    SetTextColor(hdc, kRgbNeonCyan);
    DrawTextA(hdc, "WIDGET NEXUS", -1, &titleRc, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    SelectObject(hdc, oldF);
}

static void DrawOwnerDrawButton(const DRAWITEMSTRUCT& dis) {
    auto buttonAnimProgress = [](HWND hwnd) -> float {
        for (const auto& a : g_buttonPressAnims) {
            if (a.hwnd == hwnd && a.ticksTotal > 0 && a.ticksLeft > 0) {
                return static_cast<float>(a.ticksTotal - a.ticksLeft) / static_cast<float>(a.ticksTotal);
            }
        }
        return -1.0f;
    };

    auto blendColor = [](COLORREF from, COLORREF to, float t) -> COLORREF {
        t = std::max(0.0f, std::min(1.0f, t));
        const BYTE r = static_cast<BYTE>(GetRValue(from) + (GetRValue(to) - GetRValue(from)) * t);
        const BYTE g = static_cast<BYTE>(GetGValue(from) + (GetGValue(to) - GetGValue(from)) * t);
        const BYTE b = static_cast<BYTE>(GetBValue(from) + (GetBValue(to) - GetBValue(from)) * t);
        return RGB(r, g, b);
    };

    const bool pressed = (dis.itemState & ODS_SELECTED) != 0;
    const bool focus = (dis.itemState & ODS_FOCUS) != 0;
    HDC hdc = dis.hDC;
    RECT rc = dis.rcItem;

    const float pressAnim = buttonAnimProgress(dis.hwndItem);
    const bool animatingPress = pressAnim >= 0.0f;
    const COLORREF idleFill = kRgbEdit;
    const COLORREF hotFill = kRgbControlHover;
    COLORREF dynamicFill = pressed ? hotFill : idleFill;
    if (!pressed && animatingPress) {
        // Fast "press and release" flash mapped to Minimal Pro fast timing.
        const float tri = (pressAnim < 0.5f) ? (pressAnim * 2.0f) : ((1.0f - pressAnim) * 2.0f);
        dynamicFill = blendColor(idleFill, hotFill, tri * 0.6f);
    }
    const COLORREF fillTop = LuxBlendRgb(dynamicFill, kRgbNeonCyan, pressed ? 0.05f : 0.12f);
    LuxGradientVertical(hdc, rc, fillTop, dynamicFill);

    HPEN penOuter = CreatePen(PS_SOLID, 1, LuxBlendRgb(kRgbNeonDim, kRgbDeep0, 0.3f));
    HPEN penInner = CreatePen(PS_SOLID, 1, LuxBlendRgb(kRgbNeonDim, kRgbNeonCyan, 0.55f));
    HPEN oldPenDc = static_cast<HPEN>(SelectObject(hdc, penOuter));
    HBRUSH oldBr = static_cast<HBRUSH>(SelectObject(hdc, GetStockObject(NULL_BRUSH)));
    Rectangle(hdc, dis.rcItem.left, dis.rcItem.top, dis.rcItem.right, dis.rcItem.bottom);
    RECT innerRc = dis.rcItem;
    InflateRect(&innerRc, -2, -2);
    HPEN swapOuter = static_cast<HPEN>(SelectObject(hdc, penInner));
    Rectangle(hdc, innerRc.left, innerRc.top, innerRc.right, innerRc.bottom);
    HPEN hiTop = CreatePen(PS_SOLID, 1, LuxBlendRgb(dynamicFill, RGB(255, 255, 255), 0.18f));
    HPEN swapInner = static_cast<HPEN>(SelectObject(hdc, hiTop));
    MoveToEx(hdc, innerRc.left + 1, innerRc.top + 1, nullptr);
    LineTo(hdc, innerRc.right - 2, innerRc.top + 1);
    SelectObject(hdc, swapInner);
    DeleteObject(hiTop);
    SelectObject(hdc, swapOuter);
    DeleteObject(penInner);
    SelectObject(hdc, oldPenDc);
    SelectObject(hdc, oldBr);
    DeleteObject(penOuter);
    SelectObject(hdc, GetStockObject(BLACK_PEN));

    char caption[64]{};
    GetWindowTextA(dis.hwndItem, caption, static_cast<int>(sizeof(caption)));
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, kRgbTextHi);
    HFONT oldF = static_cast<HFONT>(SelectObject(hdc, g_fontUi));
    RECT tr = dis.rcItem;
    DrawTextA(hdc, caption, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldF);

    if (focus) {
        RECT fr = dis.rcItem;
        InflateRect(&fr, -4, -4);
        DrawFocusRect(hdc, &fr);
    }
}

static void DrawOwnerDrawListItem(const DRAWITEMSTRUCT& dis) {
    auto blendColor = [](COLORREF from, COLORREF to, float t) -> COLORREF {
        t = std::max(0.0f, std::min(1.0f, t));
        const BYTE r = static_cast<BYTE>(GetRValue(from) + (GetRValue(to) - GetRValue(from)) * t);
        const BYTE g = static_cast<BYTE>(GetGValue(from) + (GetGValue(to) - GetGValue(from)) * t);
        const BYTE b = static_cast<BYTE>(GetBValue(from) + (GetBValue(to) - GetBValue(from)) * t);
        return RGB(r, g, b);
    };

    HDC hdc = dis.hDC;
    RECT rc = dis.rcItem;
    const bool selected = (dis.itemState & ODS_SELECTED) != 0;

    COLORREF bgColor = selected ? kRgbControlActive : kRgbPanel;
    const bool hasSelectAnim = (g_widgetSelectAnim.listHwnd == dis.hwndItem &&
        g_widgetSelectAnim.selectedItemData == static_cast<int>(dis.itemData) &&
        g_widgetSelectAnim.ticksLeft > 0 &&
        g_widgetSelectAnim.ticksTotal > 0);
    if (hasSelectAnim) {
        const float progress = static_cast<float>(g_widgetSelectAnim.ticksTotal - g_widgetSelectAnim.ticksLeft) /
            static_cast<float>(g_widgetSelectAnim.ticksTotal);
        const float t = std::max(0.0f, std::min(1.0f, progress));
        bgColor = blendColor(RGB(18, 22, 46), kRgbControlActive, t);
    }
    const COLORREF rowTop = LuxBlendRgb(bgColor, kRgbLuxPanelTop, selected ? 0.35f : 0.12f);
    LuxGradientVertical(hdc, rc, rowTop, bgColor);

    if (selected) {
        RECT accent{ rc.left + 3, rc.top + 4, rc.left + 7, rc.bottom - 4 };
        HBRUSH ab = CreateSolidBrush(LuxBlendRgb(kRgbNeonCyan, kRgbNeonMagenta, 0.15f));
        FillRect(hdc, &accent, ab);
        DeleteObject(ab);
        HPEN hiRow = CreatePen(PS_SOLID, 1, LuxBlendRgb(bgColor, RGB(255, 255, 255), 0.12f));
        HPEN op = static_cast<HPEN>(SelectObject(hdc, hiRow));
        MoveToEx(hdc, rc.left + 8, rc.top + 3, nullptr);
        LineTo(hdc, rc.right - 4, rc.top + 3);
        SelectObject(hdc, op);
        DeleteObject(hiRow);
    }

    HPEN pen = CreatePen(PS_SOLID, 1, selected ? LuxBlendRgb(kRgbNeonCyan, kRgbDeep0, 0.15f) : LuxBlendRgb(kRgbNeonDim, kRgbDeep0, 0.35f));
    HPEN oldP = static_cast<HPEN>(SelectObject(hdc, pen));
    HBRUSH oldB = static_cast<HBRUSH>(SelectObject(hdc, GetStockObject(NULL_BRUSH)));
    Rectangle(hdc, rc.left + 2, rc.top + 2, rc.right - 2, rc.bottom - 2);
    SelectObject(hdc, oldP);
    SelectObject(hdc, oldB);
    DeleteObject(pen);
    SelectObject(hdc, GetStockObject(BLACK_PEN));

    char buf[512]{};
    SendMessageA(dis.hwndItem, LB_GETTEXT, dis.itemID, reinterpret_cast<LPARAM>(buf));
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, selected ? kRgbNeonCyan : kRgbTextLo);
    HFONT oldF = static_cast<HFONT>(SelectObject(hdc, g_fontUi));
    RECT tr = rc;
    InflateRect(&tr, -10, 0);
    DrawTextA(hdc, buf, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(hdc, oldF);

    if ((dis.itemState & ODS_FOCUS) != 0) {
        RECT fr = dis.rcItem;
        InflateRect(&fr, -4, -4);
        DrawFocusRect(hdc, &fr);
    }
}

static std::string Trim(const std::string& s) {
    const auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static int FindGroupIndexByName(const std::string& groupName) {
    if (groupName.empty()) return -1;
    for (size_t i = 0; i < g_groups.size(); ++i) {
        if (g_groups[i].g.name == groupName) return static_cast<int>(i);
    }
    return -1;
}

static void RemoveGroupAssignment(const std::string& groupName) {
    if (groupName.empty()) return;
    for (auto& row : g_rows) {
        if (row.w.groupName == groupName) row.w.groupName.clear();
    }
}

static void EnsureDefaultWidgets() {
    if (!g_rows.empty()) return;

    Widget ssh;
    ssh.name = "SSH on WSL";
    ssh.alwaysVisible = true;
    ssh.commands.push_back("wsl.exe -e bash -lc \"ssh user@server\"");
    g_rows.push_back({ ssh, nullptr });

    Widget wsl;
    wsl.name = "Open WSL Home";
    wsl.alwaysVisible = false;
    wsl.commands.push_back("wsl.exe -e bash -lc \"cd ~ && exec bash\"");
    g_rows.push_back({ wsl, nullptr });
}

static bool LoadWidgets(const std::string& path) {
    g_rows.clear();
    g_groups.clear();
    std::ifstream in(path);
    if (!in.is_open()) {
        EnsureDefaultWidgets();
        return false;
    }

    Widget currentWidget;
    Group currentGroup;
    enum class Section { None, Widget, Group };
    Section section = Section::None;
    std::string line;
    while (std::getline(in, line)) {
        line = Trim(line);
        if (line.empty()) continue;

        if (line == "[Widget]") {
            if (section == Section::Widget) g_rows.push_back({ currentWidget, nullptr });
            else if (section == Section::Group && !currentGroup.name.empty()) g_groups.push_back({ currentGroup, nullptr });
            currentWidget = Widget{};
            section = Section::Widget;
            continue;
        }

        if (line == "[Group]") {
            if (section == Section::Widget) g_rows.push_back({ currentWidget, nullptr });
            else if (section == Section::Group && !currentGroup.name.empty()) g_groups.push_back({ currentGroup, nullptr });
            currentGroup = Group{};
            section = Section::Group;
            continue;
        }

        if (section == Section::None) continue;
        const auto pos = line.find('=');
        if (pos == std::string::npos) continue;
        const std::string key = Trim(line.substr(0, pos));
        const std::string value = Trim(line.substr(pos + 1));

        if (section == Section::Widget) {
            if (key == "name") currentWidget.name = value;
            else if (key == "alwaysVisible") currentWidget.alwaysVisible = (value == "1" || value == "true" || value == "True");
            else if (key == "group") currentWidget.groupName = value;
            else if (key == "command") currentWidget.commands.push_back(value);
            continue;
        }

        if (key == "name") currentGroup.name = value;
        else if (key == "alwaysVisible") currentGroup.alwaysVisible = (value == "1" || value == "true" || value == "True");
        else if (key == "visible") currentGroup.visible = (value == "1" || value == "true" || value == "True");
    }

    if (section == Section::Widget) g_rows.push_back({ currentWidget, nullptr });
    else if (section == Section::Group && !currentGroup.name.empty()) g_groups.push_back({ currentGroup, nullptr });

    g_groups.erase(std::remove_if(g_groups.begin(), g_groups.end(), [](const GroupRow& r) { return r.g.name.empty(); }), g_groups.end());
    g_rows.erase(std::remove_if(g_rows.begin(), g_rows.end(), [](const WidgetRow& r) { return r.w.name.empty(); }), g_rows.end());
    for (auto& row : g_rows) {
        if (FindGroupIndexByName(row.w.groupName) < 0) row.w.groupName.clear();
    }
    EnsureDefaultWidgets();
    return true;
}

static bool SaveWidgets(const std::string& path) {
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) return false;
    for (const auto& row : g_rows) {
        const Widget& w = row.w;
        out << "[Widget]\n";
        out << "name=" << w.name << "\n";
        out << "alwaysVisible=" << (w.alwaysVisible ? "1" : "0") << "\n";
        if (!w.groupName.empty()) out << "group=" << w.groupName << "\n";
        for (const auto& cmd : w.commands) out << "command=" << cmd << "\n";
        out << "\n";
    }
    for (const auto& row : g_groups) {
        const Group& g = row.g;
        out << "[Group]\n";
        out << "name=" << g.name << "\n";
        out << "alwaysVisible=" << (g.alwaysVisible ? "1" : "0") << "\n";
        out << "visible=" << (g.visible ? "1" : "0") << "\n";
        out << "\n";
    }
    return true;
}

static void SetStatus(HWND /*hWnd*/, const std::string& text) {
    if (!g_nexusHwnd) return;
    SetWindowTextA(GetDlgItem(g_nexusHwnd, IDC_STATIC_STATUS), text.c_str());
}

static std::string ReadEditText(HWND hEdit) {
    const int len = GetWindowTextLengthA(hEdit);
    std::string text(len, '\0');
    if (len > 0) GetWindowTextA(hEdit, &text[0], len + 1);
    return text;
}

static void RefreshGroupCombo(HWND hWnd) {
    HWND hCombo = GetDlgItem(hWnd, IDC_COMBO_GROUP);
    if (!hCombo) return;
    const int prevSel = static_cast<int>(SendMessageA(hCombo, CB_GETCURSEL, 0, 0));
    std::string prevName;
    if (prevSel > 0) {
        char buf[256]{};
        SendMessageA(hCombo, CB_GETLBTEXT, prevSel, reinterpret_cast<LPARAM>(buf));
        prevName = buf;
    }

    SendMessageA(hCombo, CB_RESETCONTENT, 0, 0);
    SendMessageA(hCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("(no group)"));
    for (const auto& group : g_groups) {
        SendMessageA(hCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(group.g.name.c_str()));
    }
    if (!prevName.empty()) {
        const int idx = FindGroupIndexByName(prevName);
        if (idx >= 0) SendMessageA(hCombo, CB_SETCURSEL, static_cast<WPARAM>(idx + 1), 0);
        else SendMessageA(hCombo, CB_SETCURSEL, 0, 0);
    } else {
        SendMessageA(hCombo, CB_SETCURSEL, 0, 0);
    }
}

static void WriteWidgetToEditor(HWND hWnd, int modelIndex) {
    HWND hName = GetDlgItem(hWnd, IDC_EDIT_NAME);
    HWND hAlways = GetDlgItem(hWnd, IDC_CHECK_ALWAYS);
    HWND hGroup = GetDlgItem(hWnd, IDC_COMBO_GROUP);
    HWND hCommands = GetDlgItem(hWnd, IDC_EDIT_COMMANDS);
    if (modelIndex < 0 || modelIndex >= static_cast<int>(g_rows.size())) {
        SetWindowTextA(hName, "");
        SendMessageA(hAlways, BM_SETCHECK, BST_UNCHECKED, 0);
        SendMessageA(hGroup, CB_SETCURSEL, 0, 0);
        SetWindowTextA(hCommands, "");
        return;
    }

    const Widget& w = g_rows[modelIndex].w;
    SetWindowTextA(hName, w.name.c_str());
    SendMessageA(hAlways, BM_SETCHECK, w.alwaysVisible ? BST_CHECKED : BST_UNCHECKED, 0);
    const int groupIndex = FindGroupIndexByName(w.groupName);
    SendMessageA(hGroup, CB_SETCURSEL, static_cast<WPARAM>(groupIndex >= 0 ? groupIndex + 1 : 0), 0);

    std::ostringstream oss;
    for (size_t i = 0; i < w.commands.size(); ++i) {
        oss << w.commands[i];
        if (i + 1 < w.commands.size()) oss << "\r\n";
    }
    SetWindowTextA(hCommands, oss.str().c_str());
}

static void ReadEditorToWidget(HWND hWnd, int modelIndex) {
    if (modelIndex < 0 || modelIndex >= static_cast<int>(g_rows.size())) return;
    Widget& w = g_rows[modelIndex].w;
    w.name = Trim(ReadEditText(GetDlgItem(hWnd, IDC_EDIT_NAME)));
    w.alwaysVisible = (SendMessageA(GetDlgItem(hWnd, IDC_CHECK_ALWAYS), BM_GETCHECK, 0, 0) == BST_CHECKED);
    const int groupSel = static_cast<int>(SendMessageA(GetDlgItem(hWnd, IDC_COMBO_GROUP), CB_GETCURSEL, 0, 0));
    if (groupSel <= 0) w.groupName.clear();
    else if (groupSel - 1 < static_cast<int>(g_groups.size())) w.groupName = g_groups[groupSel - 1].g.name;

    const std::string commandsText = ReadEditText(GetDlgItem(hWnd, IDC_EDIT_COMMANDS));
    std::istringstream iss(commandsText);
    std::string line;
    w.commands.clear();
    while (std::getline(iss, line)) {
        line = Trim(line);
        if (!line.empty()) w.commands.push_back(line);
    }
}

static void WriteGroupToEditor(HWND hWnd, int groupIndex) {
    HWND hName = GetDlgItem(hWnd, IDC_EDIT_GROUP_NAME);
    HWND hAlways = GetDlgItem(hWnd, IDC_CHECK_GROUP_ALWAYS);
    if (groupIndex < 0 || groupIndex >= static_cast<int>(g_groups.size())) {
        SetWindowTextA(hName, "");
        SendMessageA(hAlways, BM_SETCHECK, BST_UNCHECKED, 0);
        return;
    }
    SetWindowTextA(hName, g_groups[groupIndex].g.name.c_str());
    SendMessageA(hAlways, BM_SETCHECK, g_groups[groupIndex].g.alwaysVisible ? BST_CHECKED : BST_UNCHECKED, 0);
}

static void ReadEditorToGroup(HWND hWnd, int groupIndex) {
    if (groupIndex < 0 || groupIndex >= static_cast<int>(g_groups.size())) return;
    Group& g = g_groups[groupIndex].g;
    const std::string oldName = g.name;
    const std::string newName = Trim(ReadEditText(GetDlgItem(hWnd, IDC_EDIT_GROUP_NAME)));
    if (!newName.empty()) g.name = newName;
    g.alwaysVisible = (SendMessageA(GetDlgItem(hWnd, IDC_CHECK_GROUP_ALWAYS), BM_GETCHECK, 0, 0) == BST_CHECKED);
    if (oldName != g.name) {
        for (auto& row : g_rows) {
            if (row.w.groupName == oldName) row.w.groupName = g.name;
        }
    }
}

static void RefreshGroupList(HWND hWnd) {
    HWND hList = GetDlgItem(hWnd, IDC_LIST_GROUPS);
    SendMessageA(hList, LB_RESETCONTENT, 0, 0);
    int selectedListIndex = -1;
    for (size_t i = 0; i < g_groups.size(); ++i) {
        const Group& g = g_groups[i].g;
        std::string label = (g.visible ? "[ON ] " : "[OFF] ") + g.name + (g.alwaysVisible ? " [PIN]" : "");
        SendMessageA(hList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        SendMessageA(hList, LB_SETITEMDATA, static_cast<WPARAM>(i), static_cast<LPARAM>(i));
        if (static_cast<int>(i) == g_selectedGroupIndex) selectedListIndex = static_cast<int>(i);
    }
    if (selectedListIndex >= 0) SendMessageA(hList, LB_SETCURSEL, selectedListIndex, 0);
    else g_selectedGroupIndex = -1;
    RefreshGroupCombo(hWnd);
}

static void RefreshWidgetList(HWND hWnd) {
    HWND hList = GetDlgItem(hWnd, IDC_LIST_WIDGETS);
    SendMessageA(hList, LB_RESETCONTENT, 0, 0);

    int selectedListIndex = -1;
    for (size_t i = 0; i < g_rows.size(); ++i) {
        const Widget& w = g_rows[i].w;
        std::string label = (w.alwaysVisible ? "[PIN] " : "[   ] ") + w.name;
        if (!w.groupName.empty()) label += " {" + w.groupName + "}";
        SendMessageA(hList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        SendMessageA(hList, LB_SETITEMDATA, static_cast<WPARAM>(i), static_cast<LPARAM>(i));
        if (static_cast<int>(i) == g_selectedModelIndex) selectedListIndex = static_cast<int>(i);
    }

    if (selectedListIndex >= 0) {
        SendMessageA(hList, LB_SETCURSEL, selectedListIndex, 0);
    } else {
        g_selectedModelIndex = -1;
        WriteWidgetToEditor(hWnd, -1);
    }
}

static bool RunSingleCommand(const std::string& command, std::string& output) {
    auto toLower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    };

    const std::string cmdLower = toLower(command);
    const bool interactive =
        (cmdLower.find("wsl") != std::string::npos) ||
        (cmdLower.find("ssh ") != std::string::npos) ||
        (cmdLower.find("wt.exe") != std::string::npos);

    if (interactive) {
        // Interactive commands (like ssh password prompts) must run in a visible console.
        STARTUPINFOA si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};

        std::string cmdLine = "cmd.exe /K " + command;
        std::vector<char> mutableCmd(cmdLine.begin(), cmdLine.end());
        mutableCmd.push_back('\0');

        const BOOL ok = CreateProcessA(
            nullptr,
            mutableCmd.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NEW_CONSOLE,
            nullptr,
            nullptr,
            &si,
            &pi
        );

        if (!ok) {
            output = "Failed to launch interactive terminal command.";
            return false;
        }

        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        output = "Interactive command launched in new terminal window.";
        return true;
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hRead = nullptr, hWrite = nullptr;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return false;
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};
    std::string cmdLine = "cmd.exe /C " + command;
    std::vector<char> mutableCmd(cmdLine.begin(), cmdLine.end());
    mutableCmd.push_back('\0');

    const BOOL ok = CreateProcessA(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(hWrite);
    if (!ok) {
        CloseHandle(hRead);
        return false;
    }

    DWORD waitResult = WaitForSingleObject(pi.hProcess, INFINITE);
    (void)waitResult;

    char buffer[4096];
    DWORD bytesRead = 0;
    output.clear();
    while (ReadFile(hRead, buffer, sizeof(buffer) - 1, &bytesRead, nullptr) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        output += buffer;
    }

    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(hRead);
    return exitCode == 0;
}

static void RunWidget(int modelIndex) {
    if (modelIndex < 0 || modelIndex >= static_cast<int>(g_rows.size())) return;
    const Widget& w = g_rows[modelIndex].w;
    if (w.commands.empty()) {
        SetStatus(nullptr, "No commands to run.");
        return;
    }

    SetStatus(nullptr, "Running: " + w.name);

    for (const auto& cmd : w.commands) {
        std::string out;
        if (!RunSingleCommand(cmd, out)) {
            SetStatus(nullptr, "Failed: " + w.name);
            return;
        }
    }

    SetStatus(nullptr, "Completed: " + w.name);
}

static void UpdateFloaterIndices() {
    for (size_t j = 0; j < g_rows.size(); ++j) {
        if (g_rows[j].floater) SetWindowLongPtr(g_rows[j].floater, GWLP_USERDATA, static_cast<LONG_PTR>(j));
    }
}

static void UpdateGroupFloaterIndices() {
    for (size_t j = 0; j < g_groups.size(); ++j) {
        if (g_groups[j].floater) SetWindowLongPtr(g_groups[j].floater, GWLP_USERDATA, static_cast<LONG_PTR>(j));
    }
}

static void ApplyFloaterTopmost(size_t i) {
    if (i >= g_rows.size() || !g_rows[i].floater) return;
    SetWindowPos(
        g_rows[i].floater,
        g_rows[i].w.alwaysVisible ? HWND_TOPMOST : HWND_NOTOPMOST,
        0,
        0,
        0,
        0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

static void ApplyGroupFloaterTopmost(size_t i) {
    if (i >= g_groups.size() || !g_groups[i].floater) return;
    SetWindowPos(
        g_groups[i].floater,
        g_groups[i].g.alwaysVisible ? HWND_TOPMOST : HWND_NOTOPMOST,
        0,
        0,
        0,
        0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

static void LayoutFloatingWidgets();

static FloaterAnim* FindFloaterAnim(HWND hwnd) {
    for (auto& a : g_floaterAnims) {
        if (a.hwnd == hwnd) return &a;
    }
    return nullptr;
}

static void RemoveFloaterAnim(HWND hwnd) {
    g_floaterAnims.erase(
        std::remove_if(g_floaterAnims.begin(), g_floaterAnims.end(), [hwnd](const FloaterAnim& a) { return a.hwnd == hwnd; }),
        g_floaterAnims.end());
}

static void EnsureFloaterAnimTimer() {
    if (!g_nexusHwnd || g_floaterAnims.empty()) return;
    SetTimer(g_nexusHwnd, IDT_FLOATER_ANIM, kFloaterAnimIntervalMs, nullptr);
}

static void StopFloaterAnimTimerIfIdle() {
    if (g_nexusHwnd && g_floaterAnims.empty()) KillTimer(g_nexusHwnd, IDT_FLOATER_ANIM);
}

static void PushFloaterFadeIn(HWND hwnd) {
    if (!hwnd) return;
    RemoveFloaterAnim(hwnd);
    FloaterAnim a{};
    a.hwnd = hwnd;
    a.alpha = 0;
    a.kind = FloaterAnimKind::FadeIn;
    g_floaterAnims.push_back(a);
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    SetLayeredWindowAttributes(hwnd, 0, 0, LWA_ALPHA);
    EnsureFloaterAnimTimer();
}

static void PushFloaterFadeOut(HWND hwnd) {
    if (!hwnd) return;
    RemoveFloaterAnim(hwnd);
    FloaterAnim a{};
    a.hwnd = hwnd;
    a.alpha = kFloaterAlpha;
    a.kind = FloaterAnimKind::FadeOut;
    g_floaterAnims.push_back(a);
    SetLayeredWindowAttributes(hwnd, 0, a.alpha, LWA_ALPHA);
    EnsureFloaterAnimTimer();
}

static void PushGroupFloaterPulse(HWND hwnd) {
    if (!hwnd) return;
    RemoveFloaterAnim(hwnd);
    FloaterAnim a{};
    a.hwnd = hwnd;
    a.alpha = kFloaterAlpha;
    a.kind = FloaterAnimKind::PulseDown;
    g_floaterAnims.push_back(a);
    EnsureFloaterAnimTimer();
}

static void TickFloaterAnimations() {
    if (!g_nexusHwnd) return;
    bool layoutNeeded = false;
    const int step = static_cast<int>(kFloaterAnimStep);

    for (size_t i = 0; i < g_floaterAnims.size();) {
        FloaterAnim& a = g_floaterAnims[i];
        if (!IsWindow(a.hwnd)) {
            g_floaterAnims.erase(g_floaterAnims.begin() + i);
            continue;
        }

        switch (a.kind) {
        case FloaterAnimKind::FadeIn: {
            const int next = std::min(static_cast<int>(kFloaterAlpha), static_cast<int>(a.alpha) + step);
            a.alpha = static_cast<BYTE>(next);
            SetLayeredWindowAttributes(a.hwnd, 0, a.alpha, LWA_ALPHA);
            if (a.alpha >= kFloaterAlpha) {
                g_floaterAnims.erase(g_floaterAnims.begin() + i);
                continue;
            }
            break;
        }
        case FloaterAnimKind::FadeOut: {
            const int next = std::max(0, static_cast<int>(a.alpha) - step);
            a.alpha = static_cast<BYTE>(next);
            SetLayeredWindowAttributes(a.hwnd, 0, a.alpha, LWA_ALPHA);
            if (a.alpha <= 0) {
                SetLayeredWindowAttributes(a.hwnd, 0, kFloaterAlpha, LWA_ALPHA);
                ShowWindow(a.hwnd, SW_HIDE);
                g_floaterAnims.erase(g_floaterAnims.begin() + i);
                layoutNeeded = true;
                continue;
            }
            break;
        }
        case FloaterAnimKind::PulseDown: {
            if (static_cast<int>(a.alpha) - step <= static_cast<int>(kGroupPulseLowAlpha)) {
                a.alpha = kGroupPulseLowAlpha;
                a.kind = FloaterAnimKind::PulseUp;
            } else {
                a.alpha = static_cast<BYTE>(static_cast<int>(a.alpha) - step);
            }
            SetLayeredWindowAttributes(a.hwnd, 0, a.alpha, LWA_ALPHA);
            break;
        }
        case FloaterAnimKind::PulseUp: {
            const int next = std::min(static_cast<int>(kFloaterAlpha), static_cast<int>(a.alpha) + step);
            a.alpha = static_cast<BYTE>(next);
            SetLayeredWindowAttributes(a.hwnd, 0, a.alpha, LWA_ALPHA);
            if (a.alpha >= kFloaterAlpha) {
                g_floaterAnims.erase(g_floaterAnims.begin() + i);
                continue;
            }
            break;
        }
        }
        ++i;
    }

    StopFloaterAnimTimerIfIdle();
    if (layoutNeeded) LayoutFloatingWidgets();
}

static void EnsureUiAnimTimer() {
    if (!g_nexusHwnd) return;
    const bool haveButtonAnim = !g_buttonPressAnims.empty();
    const bool haveListAnim = (g_widgetSelectAnim.ticksLeft > 0);
    if (haveButtonAnim || haveListAnim) {
        SetTimer(g_nexusHwnd, IDT_UI_ANIM, kUiAnimIntervalMs, nullptr);
    }
}

static void StopUiAnimTimerIfIdle() {
    if (!g_nexusHwnd) return;
    const bool haveButtonAnim = !g_buttonPressAnims.empty();
    const bool haveListAnim = (g_widgetSelectAnim.ticksLeft > 0);
    if (!haveButtonAnim && !haveListAnim) {
        KillTimer(g_nexusHwnd, IDT_UI_ANIM);
    }
}

static void StartButtonPressAnim(HWND hwnd) {
    if (!hwnd) return;
    g_buttonPressAnims.erase(
        std::remove_if(g_buttonPressAnims.begin(), g_buttonPressAnims.end(), [hwnd](const ButtonPressAnim& a) { return a.hwnd == hwnd; }),
        g_buttonPressAnims.end());

    ButtonPressAnim a{};
    a.hwnd = hwnd;
    a.ticksTotal = motion::DurationToTicks(motion::Tokens::kFastMs, kUiAnimIntervalMs);
    a.ticksLeft = a.ticksTotal;
    g_buttonPressAnims.push_back(a);
    InvalidateRect(hwnd, nullptr, FALSE);
    EnsureUiAnimTimer();
}

static void StartWidgetSelectionAnim(HWND listHwnd, int selectedItemData) {
    g_widgetSelectAnim.listHwnd = listHwnd;
    g_widgetSelectAnim.selectedItemData = selectedItemData;
    g_widgetSelectAnim.ticksTotal = motion::DurationToTicks(motion::Tokens::kNormalMs, kUiAnimIntervalMs);
    g_widgetSelectAnim.ticksLeft = g_widgetSelectAnim.ticksTotal;
    if (listHwnd) InvalidateRect(listHwnd, nullptr, FALSE);
    EnsureUiAnimTimer();
}

static void TickUiAnimations() {
    for (size_t i = 0; i < g_buttonPressAnims.size();) {
        auto& a = g_buttonPressAnims[i];
        if (!a.hwnd || !IsWindow(a.hwnd)) {
            g_buttonPressAnims.erase(g_buttonPressAnims.begin() + i);
            continue;
        }
        if (a.ticksLeft > 0) --a.ticksLeft;
        InvalidateRect(a.hwnd, nullptr, FALSE);
        if (a.ticksLeft == 0) {
            g_buttonPressAnims.erase(g_buttonPressAnims.begin() + i);
            continue;
        }
        ++i;
    }

    if (g_widgetSelectAnim.ticksLeft > 0) {
        --g_widgetSelectAnim.ticksLeft;
        if (g_widgetSelectAnim.listHwnd && IsWindow(g_widgetSelectAnim.listHwnd)) {
            InvalidateRect(g_widgetSelectAnim.listHwnd, nullptr, FALSE);
        } else {
            g_widgetSelectAnim.ticksLeft = 0;
        }
    }

    StopUiAnimTimerIfIdle();
}

static bool IsWidgetVisible(const WidgetRow& row) {
    if (row.w.groupName.empty()) return row.w.alwaysVisible || g_showNonPinned;
    const int groupIndex = FindGroupIndexByName(row.w.groupName);
    if (groupIndex >= 0) return g_groups[groupIndex].g.visible;
    return row.w.alwaysVisible || g_showNonPinned;
}

static bool WidgetOccupiesLayoutSlot(const WidgetRow& row) {
    if (!row.floater) return false;
    if (IsWidgetVisible(row)) return true;
    const FloaterAnim* anim = FindFloaterAnim(row.floater);
    return anim && anim->kind == FloaterAnimKind::FadeOut;
}

static void SyncFloaterVisibility(bool animate = true) {
    if (!animate) {
        g_floaterAnims.clear();
        if (g_nexusHwnd) KillTimer(g_nexusHwnd, IDT_FLOATER_ANIM);
        for (auto& r : g_rows) {
            if (!r.floater) continue;
            const bool show = IsWidgetVisible(r);
            if (show) {
                ShowWindow(r.floater, SW_SHOWNOACTIVATE);
                SetLayeredWindowAttributes(r.floater, 0, kFloaterAlpha, LWA_ALPHA);
            } else {
                SetLayeredWindowAttributes(r.floater, 0, kFloaterAlpha, LWA_ALPHA);
                ShowWindow(r.floater, SW_HIDE);
            }
        }
        for (auto& g : g_groups) {
            if (!g.floater) continue;
            ShowWindow(g.floater, SW_SHOWNOACTIVATE);
            SetLayeredWindowAttributes(g.floater, 0, kFloaterAlpha, LWA_ALPHA);
        }
        return;
    }

    for (auto& r : g_rows) {
        if (!r.floater) continue;
        const bool want = IsWidgetVisible(r);
        const bool shown = IsWindowVisible(r.floater) != FALSE;

        if (want) {
            FloaterAnim* anim = FindFloaterAnim(r.floater);
            if (anim && anim->kind == FloaterAnimKind::FadeOut) {
                RemoveFloaterAnim(r.floater);
                PushFloaterFadeIn(r.floater);
            } else if (!shown) {
                PushFloaterFadeIn(r.floater);
            } else if (!anim) {
                SetLayeredWindowAttributes(r.floater, 0, kFloaterAlpha, LWA_ALPHA);
            }
        } else {
            FloaterAnim* anim = FindFloaterAnim(r.floater);
            if (anim && anim->kind == FloaterAnimKind::FadeIn) {
                RemoveFloaterAnim(r.floater);
                PushFloaterFadeOut(r.floater);
            } else if (shown && (!anim || anim->kind != FloaterAnimKind::FadeOut)) {
                PushFloaterFadeOut(r.floater);
            }
        }
    }
    for (auto& g : g_groups) {
        if (!g.floater) continue;
        ShowWindow(g.floater, SW_SHOWNOACTIVATE);
        if (!FindFloaterAnim(g.floater)) {
            SetLayeredWindowAttributes(g.floater, 0, kFloaterAlpha, LWA_ALPHA);
        }
    }
}

static void RefreshFloaterChrome(size_t i) {
    if (i >= g_rows.size() || !g_rows[i].floater) return;
    WidgetRow& r = g_rows[i];
    SetWindowTextA(r.floater, r.w.name.c_str());
    ApplyFloaterTopmost(i);
    InvalidateRect(r.floater, nullptr, FALSE);
}

static void RefreshGroupFloaterChrome(size_t i) {
    if (i >= g_groups.size() || !g_groups[i].floater) return;
    GroupRow& g = g_groups[i];
    std::string title = g.g.name + (g.g.visible ? " [ON]" : " [OFF]");
    SetWindowTextA(g.floater, title.c_str());
    ApplyGroupFloaterTopmost(i);
    InvalidateRect(g.floater, nullptr, FALSE);
}

static RECT GetPrimaryWorkArea() {
    RECT work{};
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0);
    return work;
}

// Unified layout order:
// 1) Group floater
// 2) Visible widgets in that group
// 3) Repeat for next group
// 4) Visible ungrouped widgets at the end
// Grid direction: top-right, fill top→bottom, then right→left.
static void LayoutFloatingWidgets() {
    const RECT work = GetPrimaryWorkArea();
    const int usableH = std::max(1, static_cast<int>(work.bottom - work.top - 2 * kFloaterMargin));
    const int colStride = std::max(kFloaterDiameter, kGroupFloaterDiameter) + kFloaterGap;
    const int rowStride = colStride;
    const int rowsPerCol = std::max(1, usableH / rowStride);
    const size_t rowsPerColumn = static_cast<size_t>(rowsPerCol);
    size_t slotIndex = 0;

    const auto placeFloater = [&](HWND floater, int diameter) {
        if (!floater) return;
        const int col = static_cast<int>(slotIndex / rowsPerColumn);
        const int row = static_cast<int>(slotIndex % rowsPerColumn);
        const int x = work.right - kFloaterMargin - diameter - col * colStride;
        const int y = work.top + kFloaterMargin + row * rowStride;
        SetWindowPos(floater, nullptr, x, y, diameter, diameter, SWP_NOACTIVATE | SWP_NOZORDER);
        ++slotIndex;
    };

    for (size_t gi = 0; gi < g_groups.size(); ++gi) {
        placeFloater(g_groups[gi].floater, kGroupFloaterDiameter);
        for (size_t wi = 0; wi < g_rows.size(); ++wi) {
            if (g_rows[wi].w.groupName != g_groups[gi].g.name) continue;
            if (!WidgetOccupiesLayoutSlot(g_rows[wi])) continue;
            placeFloater(g_rows[wi].floater, kFloaterDiameter);
        }
    }

    for (size_t wi = 0; wi < g_rows.size(); ++wi) {
        const int groupIndex = FindGroupIndexByName(g_rows[wi].w.groupName);
        if (groupIndex >= 0) continue;
        if (!WidgetOccupiesLayoutSlot(g_rows[wi])) continue;
        placeFloater(g_rows[wi].floater, kFloaterDiameter);
    }
}

static void LayoutGroupFloaters() {
    LayoutFloatingWidgets();
}

static void CreateFloaterForIndex(HINSTANCE inst, size_t i) {
    if (i >= g_rows.size()) return;
    WidgetRow& r = g_rows[i];
    if (r.floater) {
        DestroyWindow(r.floater);
        r.floater = nullptr;
    }
    const DWORD exStyle = WS_EX_LAYERED | WS_EX_TOOLWINDOW | (r.w.alwaysVisible ? WS_EX_TOPMOST : 0);
    r.floater = CreateWindowExA(
        exStyle,
        FLOATER_CLASS_NAME,
        r.w.name.c_str(),
        WS_POPUP | WS_VISIBLE,
        0,
        0,
        kFloaterDiameter,
        kFloaterDiameter,
        nullptr,
        nullptr,
        inst,
        reinterpret_cast<LPVOID>(static_cast<INT_PTR>(i)));
    if (r.floater) {
        RECT rc{};
        GetClientRect(r.floater, &rc);
        if (HRGN rgn = CreateEllipticRgn(rc.left, rc.top, rc.right, rc.bottom)) {
            SetWindowRgn(r.floater, rgn, TRUE);
        }
        SetLayeredWindowAttributes(r.floater, 0, kFloaterAlpha, LWA_ALPHA);
        SendMessageA(r.floater, WM_SETFONT, reinterpret_cast<WPARAM>(g_fontUi), TRUE);
    }
}

static void CreateGroupFloaterForIndex(HINSTANCE inst, size_t i) {
    if (i >= g_groups.size()) return;
    GroupRow& r = g_groups[i];
    if (r.floater) {
        DestroyWindow(r.floater);
        r.floater = nullptr;
    }
    const DWORD exStyle = WS_EX_LAYERED | WS_EX_TOOLWINDOW | (r.g.alwaysVisible ? WS_EX_TOPMOST : 0);
    std::string title = r.g.name + (r.g.visible ? " [ON]" : " [OFF]");
    r.floater = CreateWindowExA(
        exStyle,
        GROUP_FLOATER_CLASS_NAME,
        title.c_str(),
        WS_POPUP | WS_VISIBLE,
        0,
        0,
        kGroupFloaterDiameter,
        kGroupFloaterDiameter,
        nullptr,
        nullptr,
        inst,
        reinterpret_cast<LPVOID>(static_cast<INT_PTR>(i)));
    if (r.floater) {
        RECT rc{};
        GetClientRect(r.floater, &rc);
        if (HRGN rgn = CreateEllipticRgn(rc.left, rc.top, rc.right, rc.bottom)) {
            SetWindowRgn(r.floater, rgn, TRUE);
        }
        SetLayeredWindowAttributes(r.floater, 0, kFloaterAlpha, LWA_ALPHA);
        SendMessageA(r.floater, WM_SETFONT, reinterpret_cast<WPARAM>(g_fontUi), TRUE);
    }
}

static void DestroyAllFloaters() {
    g_floaterAnims.clear();
    if (g_nexusHwnd) KillTimer(g_nexusHwnd, IDT_FLOATER_ANIM);
    for (auto& r : g_rows) {
        if (r.floater) {
            DestroyWindow(r.floater);
            r.floater = nullptr;
        }
    }
    for (auto& g : g_groups) {
        if (g.floater) {
            DestroyWindow(g.floater);
            g.floater = nullptr;
        }
    }
}

static void RebuildAllFloaters(HINSTANCE inst) {
    DestroyAllFloaters();
    for (size_t i = 0; i < g_rows.size(); ++i) CreateFloaterForIndex(inst, i);
    for (size_t i = 0; i < g_groups.size(); ++i) CreateGroupFloaterForIndex(inst, i);
    LayoutFloatingWidgets();
    LayoutGroupFloaters();
    SyncFloaterVisibility(false);
    for (size_t i = 0; i < g_rows.size(); ++i) ApplyFloaterTopmost(i);
    for (size_t i = 0; i < g_groups.size(); ++i) ApplyGroupFloaterTopmost(i);
}

static void TrayAdd(HWND hWnd) {
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

static void TrayRemove() {
    if (!g_trayIconAdded) return;
    NOTIFYICONDATAA nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_nexusHwnd;
    nid.uID = 1;
    Shell_NotifyIconA(NIM_DELETE, &nid);
    g_trayIconAdded = false;
}

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
        // Outer ring of the circle = drag; inner disk = click to run.
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
        const long long rInner = static_cast<long long>(std::max(1, R - kFloaterDragRingPx));
        if (dist2 > rOuter * rOuter) return DefWindowProc(hwnd, msg, wParam, lParam);
        if (dist2 >= rInner * rInner) return HTCAPTION;
        return HTCLIENT;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        const HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc{};
        GetClientRect(hwnd, &rc);
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
        if (idx >= 0 && idx < static_cast<int>(g_rows.size())) name = g_rows[idx].w.name;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, kRgbNeonCyan);
        HFONT oldF = nullptr;
        if (g_fontUi) oldF = static_cast<HFONT>(SelectObject(hdc, g_fontUi));
        RECT textRc = rc;
        InflateRect(&textRc, -10, -10);
        DrawTextA(hdc, name.c_str(), -1, &textRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        if (oldF) SelectObject(hdc, oldF);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONUP: {
        const int idx = static_cast<int>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        if (idx >= 0 && idx < static_cast<int>(g_rows.size())) RunWidget(idx);
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
        // Outer ring drags the group floater; inner disk acts as toggle button.
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
        const long long rInner = static_cast<long long>(std::max(1, R - kFloaterDragRingPx));
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
        const bool isVisible = (idx >= 0 && idx < static_cast<int>(g_groups.size())) ? g_groups[idx].g.visible : false;

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
        if (idx >= 0 && idx < static_cast<int>(g_groups.size())) title = g_groups[idx].g.name;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, kRgbNeonCyan);
        HFONT oldF = nullptr;
        if (g_fontUi) oldF = static_cast<HFONT>(SelectObject(hdc, g_fontUi));
        RECT textRc = rc;
        InflateRect(&textRc, -12, -12);
        DrawTextA(hdc, title.c_str(), -1, &textRc, DT_CENTER | DT_VCENTER | DT_WORDBREAK | DT_END_ELLIPSIS | DT_NOPREFIX);
        if (oldF) SelectObject(hdc, oldF);

        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONUP: {
        const int idx = static_cast<int>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        if (idx >= 0 && idx < static_cast<int>(g_groups.size())) {
            g_groups[idx].g.visible = !g_groups[idx].g.visible;
            RefreshGroupFloaterChrome(static_cast<size_t>(idx));
            if (g_groups[idx].floater) PushGroupFloaterPulse(g_groups[idx].floater);
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

static void SaveCurrentEditorWidget(HWND hWnd) {
    if (g_selectedModelIndex >= 0 && g_selectedModelIndex < static_cast<int>(g_rows.size())) {
        ReadEditorToWidget(hWnd, g_selectedModelIndex);
        RefreshFloaterChrome(static_cast<size_t>(g_selectedModelIndex));
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
            AppendMenuA(m, MF_STRING, kTrayCmdShow, "Show Nexus");
            AppendMenuA(m, MF_STRING, kTrayCmdAbout, "About Widget Nexus…");
            AppendMenuA(m, MF_SEPARATOR, 0, nullptr);
            AppendMenuA(m, MF_STRING, kTrayCmdExit, "Exit");
            SetForegroundWindow(hWnd);
            const UINT cmd = TrackPopupMenu(m, TPM_RIGHTALIGN | TPM_BOTTOMALIGN | TPM_RETURNCMD, pt.x, pt.y, 0, hWnd, nullptr);
            DestroyMenu(m);
            if (cmd == kTrayCmdShow) {
                ShowWindow(hWnd, SW_SHOW);
                SetForegroundWindow(hWnd);
            } else if (cmd == kTrayCmdAbout) {
                ShowAboutWidgetNexus(hWnd);
            } else if (cmd == kTrayCmdExit) {
                DestroyWindow(hWnd);
            }
            return 0;
        }
        return 0;
    }

    switch (msg) {
    case WM_CREATE: {
        g_nexusHwnd = hWnd;
        const auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        g_hInstance = cs->hInstance;

        InitCommonControls();
        ThemeCreate();

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
            RIGHT_X, TOP + 4, 60, 22, hWnd, nullptr, nullptr, nullptr);
        ApplyNeonControlSkin(hLblName);
        HWND hName = CreateWindowA("EDIT", "", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
            RIGHT_X + 60, TOP + 2, 188, 28, hWnd, reinterpret_cast<HMENU>(IDC_EDIT_NAME), nullptr, nullptr);
        ApplyNeonControlSkin(hName);

        HWND hLblGroup = CreateWindowA("STATIC", "Group:", WS_VISIBLE | WS_CHILD,
            RIGHT_X + 256, TOP + 4, 52, 22, hWnd, nullptr, nullptr, nullptr);
        ApplyNeonControlSkin(hLblGroup);
        HWND hGroup = CreateWindowA("COMBOBOX", "", WS_VISIBLE | WS_CHILD | WS_BORDER | CBS_DROPDOWNLIST,
            RIGHT_X + 308, TOP + 2, 256, 380, hWnd, reinterpret_cast<HMENU>(IDC_COMBO_GROUP), nullptr, nullptr);
        ApplyNeonControlSkin(hGroup);

        HWND hAlways = CreateWindowA("BUTTON", "Always visible (pinned)", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
            RIGHT_X + 60, TOP + 36, 240, 24, hWnd, reinterpret_cast<HMENU>(IDC_CHECK_ALWAYS), nullptr, nullptr);
        ApplyNeonControlSkin(hAlways);

        HWND hLblCmd = CreateWindowA("STATIC", "Commands (one per line):", WS_VISIBLE | WS_CHILD,
            RIGHT_X, TOP + 72, 210, 22, hWnd, nullptr, nullptr, nullptr);
        ApplyNeonControlSkin(hLblCmd);
        HWND hCmd = CreateWindowA("EDIT", "", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_LEFT | ES_MULTILINE | WS_VSCROLL | ES_AUTOVSCROLL,
            RIGHT_X, TOP + 96, 430, 282, hWnd, reinterpret_cast<HMENU>(IDC_EDIT_COMMANDS), nullptr, nullptr);
        ApplyNeonControlSkin(hCmd);
        SendMessageA(hCmd, WM_SETFONT, reinterpret_cast<WPARAM>(g_fontMono), TRUE);

        HWND hLblGroups = CreateWindowA("STATIC", "Groups:", WS_VISIBLE | WS_CHILD,
            RIGHT_X + 438, TOP + 72, 120, 22, hWnd, nullptr, nullptr, nullptr);
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

        LoadWidgets(g_configPath);
        RebuildAllFloaters(g_hInstance);
        RefreshGroupList(hWnd);
        RefreshWidgetList(hWnd);
        RefreshGroupCombo(hWnd);
        SetStatus(hWnd, "Loaded widgets from widgets.txt");
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }

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
                id == IDC_BTN_HIDE_NEXUS || id == IDC_BTN_ADD_GROUP || id == IDC_BTN_DELETE_GROUP;
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
            const int prevSel = g_selectedGroupIndex;
            HWND hList = GetDlgItem(hWnd, IDC_LIST_GROUPS);
            const int sel = static_cast<int>(SendMessageA(hList, LB_GETCURSEL, 0, 0));
            if (prevSel >= 0 && prevSel < static_cast<int>(g_groups.size())) {
                ReadEditorToGroup(hWnd, prevSel);
                RefreshGroupFloaterChrome(static_cast<size_t>(prevSel));
            }
            if (sel != LB_ERR) {
                g_selectedGroupIndex = static_cast<int>(SendMessageA(hList, LB_GETITEMDATA, sel, 0));
                WriteGroupToEditor(hWnd, g_selectedGroupIndex);
            } else {
                g_selectedGroupIndex = -1;
                WriteGroupToEditor(hWnd, -1);
            }
            RefreshGroupList(hWnd);
            RefreshWidgetList(hWnd);
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
                if (g_rows[g_selectedModelIndex].floater) {
                    DestroyWindow(g_rows[g_selectedModelIndex].floater);
                    g_rows[g_selectedModelIndex].floater = nullptr;
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
                const std::string name = g_groups[g_selectedGroupIndex].g.name;
                if (g_groups[g_selectedGroupIndex].floater) {
                    DestroyWindow(g_groups[g_selectedGroupIndex].floater);
                    g_groups[g_selectedGroupIndex].floater = nullptr;
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
            if (SaveWidgets(g_configPath)) SetStatus(hWnd, "Saved to widgets.txt");
            else SetStatus(hWnd, "Failed to save widgets.txt");
            return 0;
        }

        if (id == IDC_BTN_RUN && code == BN_CLICKED) {
            SaveCurrentEditorWidget(hWnd);
            RefreshWidgetList(hWnd);
            RunWidget(g_selectedModelIndex);
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
    g_hInstance = hInstance;
    InitConfigPathBesideExe();

    const char CLASS_NAME[] = "WidgetLauncherWin32";

    g_windowIcon = CreateNeonWlIcon(32);
    g_windowIconSm = CreateNeonWlIcon(16);

    WNDCLASSA wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
    // Icons are applied with WM_SETICON so we retain ownership and can DestroyIcon in WM_DESTROY.

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

    HWND hWnd = CreateWindowExA(
        0, CLASS_NAME, "WIDGET NEXUS",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 960, 700,
        nullptr, nullptr, hInstance, nullptr
    );

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
    UpdateWindow(hWnd);

    MSG msg{};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
