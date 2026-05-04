#pragma once

#include <windows.h>

// Style spec v2 — see STYLE_SPEC_V2.md (surfaces, accents, text)
inline constexpr COLORREF kRgbDeep0 = RGB(8, 10, 20);
inline constexpr COLORREF kRgbPanel = RGB(16, 20, 42);
inline constexpr COLORREF kRgbEdit = RGB(12, 16, 34);
inline constexpr COLORREF kRgbControlHover = RGB(18, 24, 48);
inline constexpr COLORREF kRgbControlActive = RGB(24, 34, 66);
inline constexpr COLORREF kRgbNeonCyan = RGB(0, 220, 235);
inline constexpr COLORREF kRgbNeonMagenta = RGB(235, 72, 198);
inline constexpr COLORREF kRgbNeonDim = RGB(0, 128, 158);
inline constexpr COLORREF kRgbTextHi = RGB(224, 246, 255);
inline constexpr COLORREF kRgbTextLo = RGB(154, 188, 208);
inline constexpr COLORREF kRgbGroupOnFill = RGB(18, 48, 30);
inline constexpr COLORREF kRgbGroupOffFill = RGB(48, 22, 26);
inline constexpr COLORREF kRgbLuxCanvasTop = RGB(18, 22, 46);
inline constexpr COLORREF kRgbLuxCanvasBot = RGB(6, 8, 17);
inline constexpr COLORREF kRgbLuxPanelTop = RGB(28, 34, 62);
inline constexpr COLORREF kRgbLuxShadow = RGB(3, 4, 12);
inline constexpr COLORREF kRgbLuxVignetteEdge = RGB(4, 5, 14);
inline constexpr COLORREF kRgbBgStatus = RGB(10, 14, 30);

COLORREF LuxBlendRgb(COLORREF a, COLORREF b, float t);
void LuxGradientVertical(HDC hdc, const RECT& rc, COLORREF top, COLORREF bottom);
void LuxGradientHorizontal(HDC hdc, const RECT& rc, COLORREF left, COLORREF rightC);
void PaintLuxuryPanelCard(HDC hdc, const RECT& panelRc);
void DrawLuxurySectionCaption(HDC hdc, const RECT& panelRc, const char* text, int baselineY);

void ThemeCreate(UINT dpi);
void ThemeDestroy();
void ApplyNeonControlSkin(HWND hCtrl);
void PaintNeonWindow(HWND hWnd, HDC hdc);
void DrawOwnerDrawButton(const DRAWITEMSTRUCT& dis);
void DrawOwnerDrawListItem(const DRAWITEMSTRUCT& dis);
HICON CreateNeonWlIcon(int pixelSize);
void ShowAboutWidgetNexus(HWND owner);

enum class StylePreset : int {
    NeonNight = 0,
    CyberIndigo = 1,
    MinimalDark = 2
};

void CycleStylePreset();
const char* CurrentStylePresetName();
void SetCompactDensity(bool compact);
