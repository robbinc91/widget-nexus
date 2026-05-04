#include "nexus_paint.hpp"

#include "app_ids.hpp" // kAppVersion, kAppRepoUrl
#include "dpi.hpp"
#include "globals.hpp"

#include <shellapi.h>
#include <uxtheme.h>

#include <algorithm>
#include <string>

// MinGW often omits msimg32.h even though -lmsimg32 resolves GradientFill.
#ifndef GRADIENT_FILL_RECT_H
#define GRADIENT_FILL_RECT_H 0x00000000
#endif
#ifndef GRADIENT_FILL_RECT_V
#define GRADIENT_FILL_RECT_V 0x00000001
#endif

using GradientFillFn = WINBOOL(WINAPI*)(HDC, PTRIVERTEX, ULONG, PVOID, ULONG, ULONG);

static bool CallGradientFill(HDC hdc, PTRIVERTEX pVertex, ULONG nVertex, PVOID pMesh, ULONG nMesh, ULONG ulMode) {
    static GradientFillFn fn = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        HMODULE mod = GetModuleHandleA("msimg32.dll");
        if (!mod) mod = LoadLibraryA("msimg32.dll");
        if (mod) fn = reinterpret_cast<GradientFillFn>(GetProcAddress(mod, "GradientFill"));
    }
    return fn != nullptr && fn(hdc, pVertex, nVertex, pMesh, nMesh, ulMode);
}

namespace {

StylePreset g_stylePreset = StylePreset::NeonNight;
bool g_compactStyleDensity = false;

struct ThemeVariant {
    COLORREF canvasTop;
    COLORREF canvasBottom;
    COLORREF panelTop;
    COLORREF panelFill;
    COLORREF accentA;
    COLORREF accentB;
    COLORREF textLo;
};

ThemeVariant CurrentVariant() {
    switch (g_stylePreset) {
    case StylePreset::CyberIndigo:
        return { RGB(22, 20, 56), RGB(8, 10, 26), RGB(34, 36, 82), RGB(18, 20, 54), RGB(94, 146, 255), RGB(184, 88, 255), RGB(164, 178, 220) };
    case StylePreset::MinimalDark:
        return { RGB(18, 20, 28), RGB(10, 11, 17), RGB(30, 32, 40), RGB(18, 20, 30), RGB(95, 195, 210), RGB(145, 155, 195), RGB(168, 176, 192) };
    case StylePreset::NeonNight:
    default:
        return { kRgbLuxCanvasTop, kRgbLuxCanvasBot, kRgbLuxPanelTop, kRgbPanel, kRgbNeonCyan, kRgbNeonMagenta, kRgbTextLo };
    }
}
} // namespace

COLORREF LuxBlendRgb(COLORREF a, COLORREF b, float t) {
    t = std::max(0.0f, std::min(1.0f, t));
    const BYTE r = static_cast<BYTE>(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * t);
    const BYTE gg = static_cast<BYTE>(GetGValue(a) + (GetGValue(b) - GetGValue(a)) * t);
    const BYTE bb = static_cast<BYTE>(GetBValue(a) + (GetBValue(b) - GetBValue(a)) * t);
    return RGB(r, gg, bb);
}

void LuxGradientVertical(HDC hdc, const RECT& rc, COLORREF top, COLORREF bottom) {
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
    (void)CallGradientFill(hdc, tv, 2, &gr, 1, GRADIENT_FILL_RECT_V);
}

void LuxGradientHorizontal(HDC hdc, const RECT& rc, COLORREF left, COLORREF rightC) {
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
    (void)CallGradientFill(hdc, tv, 2, &gr, 1, GRADIENT_FILL_RECT_H);
}

void PaintLuxuryPanelCard(HDC hdc, const RECT& panelRc) {
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

void DrawLuxurySectionCaption(HDC hdc, const RECT& panelRc, const char* text, int baselineY) {
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

HICON CreateNeonWlIcon(int pixelSize) {
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

void ShowAboutWidgetNexus(HWND owner) {
    std::string text = "Widget Nexus — desktop widget launcher.\r\n\r\nVersion ";
    text += kAppVersion;
    text += "\r\n\r\nRepository:\r\n";
    text += kAppRepoUrl;
    text += "\r\n\r\nOpen the repository page in your browser now?";
    if (MessageBoxA(owner, text.c_str(), "About Widget Nexus", MB_YESNO | MB_ICONINFORMATION) == IDYES) {
        ShellExecuteA(owner, "open", kAppRepoUrl, nullptr, nullptr, SW_SHOWNORMAL);
    }
}

void ThemeCreate(UINT dpi) {
    ThemeDestroy();
    const int d = static_cast<int>(dpi);
    const ThemeVariant v = CurrentVariant();
    g_brDeep = CreateSolidBrush(kRgbDeep0);
    g_brPanel = CreateSolidBrush(v.panelFill);
    g_brEdit = CreateSolidBrush(kRgbEdit);
    g_brListSel = CreateSolidBrush(kRgbControlActive);
    g_brBtn = CreateSolidBrush(kRgbEdit);
    g_brBtnHot = CreateSolidBrush(kRgbControlHover);
    g_penFrame = CreatePen(PS_SOLID, 1, v.accentA);
    g_penGlow = CreatePen(PS_SOLID, 1, LuxBlendRgb(v.accentA, v.accentB, 0.35f));
    const int titlePt = g_compactStyleDensity ? 22 : 26;
    const int uiPt = g_compactStyleDensity ? 13 : 15;
    const int monoPt = g_compactStyleDensity ? 12 : 14;
    const int secPt = g_compactStyleDensity ? 11 : 12;
    g_fontTitle = CreateFontA(MulDiv(titlePt, d, 96), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
    g_fontUi = CreateFontA(MulDiv(uiPt, d, 96), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
    g_fontMono = CreateFontA(MulDiv(monoPt, d, 96), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
    g_fontSection = CreateFontA(MulDiv(secPt, d, 96), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
    g_brStatus = CreateSolidBrush(kRgbBgStatus);
    g_listItemHeight = MulDiv(g_compactStyleDensity ? 34 : 40, d, 96);
}

void ThemeDestroy() {
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

void ApplyNeonControlSkin(HWND hCtrl) {
    if (!hCtrl) return;
    SetWindowTheme(hCtrl, L"", L"");
    SendMessageA(hCtrl, WM_SETFONT, reinterpret_cast<WPARAM>(g_fontUi), TRUE);
}

void PaintNeonWindow(HWND hWnd, HDC hdc) {
    const ThemeVariant v = CurrentVariant();
    RECT rc{};
    GetClientRect(hWnd, &rc);
    LuxGradientVertical(hdc, rc, v.canvasTop, v.canvasBottom);

    const int canvasW = static_cast<int>(rc.right - rc.left);
    const int vignetteW = std::min(ScaleByDpi(72), canvasW / 6);
    if (vignetteW > 8) {
        RECT vl{ rc.left, rc.top, rc.left + vignetteW, rc.bottom };
        LuxGradientHorizontal(hdc, vl, kRgbLuxVignetteEdge, LuxBlendRgb(v.canvasTop, v.canvasBottom, 0.5f));
        RECT vr{ rc.right - vignetteW, rc.top, rc.right, rc.bottom };
        LuxGradientHorizontal(hdc, vr, LuxBlendRgb(v.canvasTop, v.canvasBottom, 0.5f), kRgbLuxVignetteEdge);
    }

    const int headerClipH = ScaleByDpi(58);
    const int savedDc = SaveDC(hdc);
    IntersectClipRect(hdc, rc.left, rc.top, rc.right, headerClipH);
    for (int y = 0; y < headerClipH; y += 3) {
        const int t = (y * 255) / std::max(1, headerClipH);
        const COLORREF c = RGB(14 + t / 24, 18 + t / 28, 36 + t / 10);
        RECT strip{ rc.left, y, rc.right, y + 3 };
        HBRUSH b = CreateSolidBrush(c);
        FillRect(hdc, &strip, b);
        DeleteObject(b);
    }
    if (savedDc) RestoreDC(hdc, savedDc);

    const int m = ScaleByDpi(8);
    const int top = ScaleByDpi(74);
    const int bottomPad = ScaleByDpi(44);
    const int panelGap = ScaleByDpi(12);
    const int panelW = static_cast<int>(rc.right - rc.left) - m * 2;
    const int leftW = std::max(ScaleByDpi(250), std::min(ScaleByDpi(430), panelW * 35 / 100));
    const int splitX = static_cast<int>(rc.left) + m + leftW;
    RECT leftPanel{ m, top, splitX, rc.bottom - bottomPad };
    RECT rightPanel{ splitX + panelGap, top, rc.right - m, rc.bottom - bottomPad };
    PaintLuxuryPanelCard(hdc, leftPanel);
    PaintLuxuryPanelCard(hdc, rightPanel);

    // Soft glass highlight to increase panel depth.
    RECT glass{ m + ScaleByDpi(2), top + ScaleByDpi(2), rc.right - m - ScaleByDpi(2), top + ScaleByDpi(26) };
    LuxGradientVertical(hdc, glass, LuxBlendRgb(v.accentA, RGB(255, 255, 255), 0.12f), RGB(0, 0, 0));

    const int kGroupsColumnX =
        static_cast<int>(rightPanel.left) + std::max(ScaleByDpi(170), static_cast<int>((rightPanel.right - rightPanel.left) * 70 / 100));
    DrawLuxurySectionCaption(hdc, leftPanel, "WIDGETS", leftPanel.top + ScaleByDpi(6));
    const int detailsRight = std::min(kGroupsColumnX - ScaleByDpi(6), static_cast<int>(rightPanel.right));
    RECT detailsBand{ rightPanel.left, rightPanel.top, detailsRight, rightPanel.bottom };
    DrawLuxurySectionCaption(hdc, detailsBand, "DETAILS", rightPanel.top + ScaleByDpi(6));
    const int groupsLo = rightPanel.left + ScaleByDpi(12);
    const int groupsHi = std::max(groupsLo + ScaleByDpi(44), static_cast<int>(rightPanel.right) - ScaleByDpi(40));
    const int groupsLeft = std::clamp(kGroupsColumnX - ScaleByDpi(8), groupsLo, groupsHi);
    RECT groupsBand{ groupsLeft, rightPanel.top, rightPanel.right, rightPanel.bottom };
    if (groupsBand.left < groupsBand.right - ScaleByDpi(40)) {
        DrawLuxurySectionCaption(hdc, groupsBand, "GROUPS", rightPanel.top + ScaleByDpi(6));
    }

    RECT statusStrip{ rc.left + m, rc.bottom - ScaleByDpi(40), rc.right - m, rc.bottom - ScaleByDpi(8) };
    FillRect(hdc, &statusStrip, g_brStatus ? g_brStatus : g_brDeep);
    HPEN statusHi = CreatePen(PS_SOLID, 1, LuxBlendRgb(kRgbNeonDim, kRgbDeep0, 0.5f));
    HPEN oldStat = static_cast<HPEN>(SelectObject(hdc, statusHi));
    MoveToEx(hdc, statusStrip.left, statusStrip.top, nullptr);
    LineTo(hdc, statusStrip.right, statusStrip.top);
    SelectObject(hdc, oldStat);
    DeleteObject(statusHi);

    const int frameM = ScaleByDpi(8);
    HPEN outerPen = CreatePen(PS_SOLID, 1, kRgbNeonDim);
    HPEN penRestore = static_cast<HPEN>(SelectObject(hdc, outerPen));
    HBRUSH oldBrFrame = static_cast<HBRUSH>(SelectObject(hdc, GetStockObject(NULL_BRUSH)));
    Rectangle(hdc, rc.left + frameM, rc.top + frameM, rc.right - frameM, rc.bottom - frameM);
    HPEN innerGlow = CreatePen(PS_SOLID, 1, LuxBlendRgb(kRgbNeonCyan, kRgbDeep0, 0.55f));
    HPEN penOuterHeld = static_cast<HPEN>(SelectObject(hdc, innerGlow));
    const int innerPad = ScaleByDpi(18);
    MoveToEx(hdc, innerPad, rc.top + frameM + 1, nullptr);
    LineTo(hdc, rc.right - innerPad, rc.top + frameM + 1);
    SelectObject(hdc, penOuterHeld);
    DeleteObject(innerGlow);
    SelectObject(hdc, penRestore);
    SelectObject(hdc, oldBrFrame);
    DeleteObject(outerPen);

    HPEN mag = CreatePen(PS_SOLID, 1, v.accentB);
    HPEN prevForMag = static_cast<HPEN>(SelectObject(hdc, mag));
    MoveToEx(hdc, innerPad, ScaleByDpi(52), nullptr);
    LineTo(hdc, rc.right - innerPad, ScaleByDpi(52));
    SelectObject(hdc, prevForMag);
    DeleteObject(mag);

    SetBkMode(hdc, TRANSPARENT);
    HFONT oldF = static_cast<HFONT>(SelectObject(hdc, g_fontTitle));
    RECT titleRc{ innerPad, ScaleByDpi(10), rc.right - innerPad, ScaleByDpi(52) };
    SetTextColor(hdc, RGB(0, 72, 88));
    RECT shadow = titleRc;
    OffsetRect(&shadow, 2, 2);
    DrawTextA(hdc, "WIDGET NEXUS", -1, &shadow, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    SetTextColor(hdc, v.accentA);
    DrawTextA(hdc, "WIDGET NEXUS", -1, &titleRc, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    RECT subtitle{ innerPad + ScaleByDpi(2), ScaleByDpi(44), rc.right - innerPad, ScaleByDpi(64) };
    HFONT oldSec = static_cast<HFONT>(SelectObject(hdc, g_fontSection ? g_fontSection : g_fontUi));
    SetTextColor(hdc, v.textLo);
    const char* styleName = CurrentStylePresetName();
    DrawTextA(hdc, styleName, -1, &subtitle, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    SelectObject(hdc, oldSec);
    SelectObject(hdc, oldF);
}

void DrawOwnerDrawButton(const DRAWITEMSTRUCT& dis) {
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
    SetTextColor(hdc, LuxBlendRgb(kRgbTextHi, CurrentVariant().accentA, 0.08f));
    HFONT oldFf = static_cast<HFONT>(SelectObject(hdc, g_fontUi));
    RECT tr = dis.rcItem;
    DrawTextA(hdc, caption, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldFf);

    if (focus) {
        RECT fr = dis.rcItem;
        InflateRect(&fr, -4, -4);
        DrawFocusRect(hdc, &fr);
    }
}

void DrawOwnerDrawListItem(const DRAWITEMSTRUCT& dis) {
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
    const ThemeVariant v = CurrentVariant();
    SetTextColor(hdc, selected ? v.accentA : v.textLo);
    HFONT oldFf = static_cast<HFONT>(SelectObject(hdc, g_fontUi));
    RECT tr = rc;
    InflateRect(&tr, -10, 0);
    DrawTextA(hdc, buf, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(hdc, oldFf);

    if ((dis.itemState & ODS_FOCUS) != 0) {
        RECT fr = dis.rcItem;
        InflateRect(&fr, -4, -4);
        DrawFocusRect(hdc, &fr);
    }
}

void CycleStylePreset() {
    const int next = (static_cast<int>(g_stylePreset) + 1) % 3;
    g_stylePreset = static_cast<StylePreset>(next);
}

const char* CurrentStylePresetName() {
    switch (g_stylePreset) {
    case StylePreset::CyberIndigo: return "CYBER INDIGO";
    case StylePreset::MinimalDark: return "MINIMAL DARK";
    case StylePreset::NeonNight:
    default: return "NEON NIGHT";
    }
}

void SetCompactDensity(bool compact) {
    g_compactStyleDensity = compact;
}
