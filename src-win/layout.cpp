#include "layout.hpp"

#include "../motion_tokens.h"
#include "app_ids.hpp"
#include "dpi.hpp"
#include "globals.hpp"

#include <algorithm>

namespace {

constexpr BYTE kFloaterAnimStep = motion::AlphaStep(
    0,
    kFloaterAlpha,
    motion::DurationToTicks(motion::Tokens::kPanelMs, kFloaterAnimIntervalMs));

BYTE GroupPulseLowAlpha() { return static_cast<BYTE>((static_cast<unsigned int>(kFloaterAlpha) * 62u) / 100u); }

FloaterAnim* FindFloaterAnim(HWND hwnd) {
    for (auto& a : g_floaterAnims) {
        if (a.hwnd == hwnd) return &a;
    }
    return nullptr;
}

void RemoveFloaterAnim(HWND hwnd) {
    g_floaterAnims.erase(
        std::remove_if(g_floaterAnims.begin(), g_floaterAnims.end(), [hwnd](const FloaterAnim& a) { return a.hwnd == hwnd; }),
        g_floaterAnims.end());
}

int FindGroupIndexByNameLocal(const std::string& groupName) {
    if (groupName.empty()) return -1;
    for (size_t i = 0; i < g_groups.size(); ++i) {
        if (g_groups[i].g.name == groupName) return static_cast<int>(i);
    }
    return -1;
}

bool IsWidgetVisible(const WidgetRow& row) {
    if (row.w.groupName.empty()) return row.w.alwaysVisible || g_showNonPinned;
    const int groupIndex = FindGroupIndexByNameLocal(row.w.groupName);
    if (groupIndex >= 0) return g_groups[static_cast<size_t>(groupIndex)].g.visible;
    return row.w.alwaysVisible || g_showNonPinned;
}

bool WidgetOccupiesLayoutSlot(const WidgetRow& row) {
    if (!row.floater) return false;
    if (IsWidgetVisible(row)) return true;
    const FloaterAnim* anim = FindFloaterAnim(row.floater);
    return anim && anim->kind == FloaterAnimKind::FadeOut;
}

} // namespace

int FloaterDiameterPx() { return ScaleByDpi(kFloaterDiameter96); }
int GroupFloaterDiameterPx() { return ScaleByDpi(kGroupFloaterDiameter96); }
int FloaterDragRingPx() { return ScaleByDpi(kFloaterDragRingPx96); }

void UpdateFloaterIndices() {
    for (size_t j = 0; j < g_rows.size(); ++j) {
        if (g_rows[j].floater) SetWindowLongPtr(g_rows[j].floater, GWLP_USERDATA, static_cast<LONG_PTR>(j));
    }
}

void UpdateGroupFloaterIndices() {
    for (size_t j = 0; j < g_groups.size(); ++j) {
        if (g_groups[j].floater) SetWindowLongPtr(g_groups[j].floater, GWLP_USERDATA, static_cast<LONG_PTR>(j));
    }
}

void ApplyFloaterTopmost(size_t i) {
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

void ApplyGroupFloaterTopmost(size_t i) {
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

void EnsureFloaterAnimTimer() {
    if (!g_nexusHwnd || g_floaterAnims.empty()) return;
    SetTimer(g_nexusHwnd, IDT_FLOATER_ANIM, kFloaterAnimIntervalMs, nullptr);
}

void StopFloaterAnimTimerIfIdle() {
    if (g_nexusHwnd && g_floaterAnims.empty()) KillTimer(g_nexusHwnd, IDT_FLOATER_ANIM);
}

void PushFloaterFadeIn(HWND hwnd) {
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

void PushFloaterFadeOut(HWND hwnd) {
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

void PushGroupFloaterPulse(HWND hwnd) {
    if (!hwnd) return;
    RemoveFloaterAnim(hwnd);
    FloaterAnim a{};
    a.hwnd = hwnd;
    a.alpha = kFloaterAlpha;
    a.kind = FloaterAnimKind::PulseDown;
    g_floaterAnims.push_back(a);
    EnsureFloaterAnimTimer();
}

void TickFloaterAnimations() {
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
            const int low = static_cast<int>(GroupPulseLowAlpha());
            if (static_cast<int>(a.alpha) - step <= low) {
                a.alpha = static_cast<BYTE>(low);
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

void EnsureUiAnimTimer() {
    if (!g_nexusHwnd) return;
    const bool haveButtonAnim = !g_buttonPressAnims.empty();
    const bool haveListAnim = (g_widgetSelectAnim.ticksLeft > 0);
    if (haveButtonAnim || haveListAnim) {
        SetTimer(g_nexusHwnd, IDT_UI_ANIM, kUiAnimIntervalMs, nullptr);
    }
}

void StopUiAnimTimerIfIdle() {
    if (!g_nexusHwnd) return;
    const bool haveButtonAnim = !g_buttonPressAnims.empty();
    const bool haveListAnim = (g_widgetSelectAnim.ticksLeft > 0);
    if (!haveButtonAnim && !haveListAnim) {
        KillTimer(g_nexusHwnd, IDT_UI_ANIM);
    }
}

void StartButtonPressAnim(HWND hwnd) {
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

void StartWidgetSelectionAnim(HWND listHwnd, int selectedItemData) {
    g_widgetSelectAnim.listHwnd = listHwnd;
    g_widgetSelectAnim.selectedItemData = selectedItemData;
    g_widgetSelectAnim.ticksTotal = motion::DurationToTicks(motion::Tokens::kNormalMs, kUiAnimIntervalMs);
    g_widgetSelectAnim.ticksLeft = g_widgetSelectAnim.ticksTotal;
    if (listHwnd) InvalidateRect(listHwnd, nullptr, FALSE);
    EnsureUiAnimTimer();
}

void TickUiAnimations() {
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

void SyncFloaterVisibility(bool animate) {
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

void RefreshFloaterChrome(size_t i) {
    if (i >= g_rows.size() || !g_rows[i].floater) return;
    WidgetRow& r = g_rows[i];
    SetWindowTextA(r.floater, r.w.name.c_str());
    ApplyFloaterTopmost(i);
    InvalidateRect(r.floater, nullptr, FALSE);
}

void RefreshGroupFloaterChrome(size_t i) {
    if (i >= g_groups.size() || !g_groups[i].floater) return;
    GroupRow& g = g_groups[i];
    std::string title = g.g.name + (g.g.visible ? " [ON]" : " [OFF]");
    SetWindowTextA(g.floater, title.c_str());
    ApplyGroupFloaterTopmost(i);
    InvalidateRect(g.floater, nullptr, FALSE);
}

RECT GetPrimaryWorkArea() {
    RECT work{};
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0);
    return work;
}

void LayoutFloatingWidgets() {
    const RECT work = GetPrimaryWorkArea();
    const int margin = ScaleByDpi(kFloaterMargin96);
    const int gap = ScaleByDpi(kFloaterGap96);
    const int dW = FloaterDiameterPx();
    const int dG = GroupFloaterDiameterPx();
    const int usableH = std::max(1, static_cast<int>(work.bottom - work.top - 2 * margin));
    const int colStride = std::max(dW, dG) + gap;
    const int rowStride = colStride;
    const int rowsPerCol = std::max(1, usableH / rowStride);
    const size_t rowsPerColumn = static_cast<size_t>(rowsPerCol);
    size_t slotIndex = 0;

    const auto placeFloater = [&](HWND floater, int diameter) {
        if (!floater) return;
        const int col = static_cast<int>(slotIndex / rowsPerColumn);
        const int row = static_cast<int>(slotIndex % rowsPerColumn);
        const int x = work.right - margin - diameter - col * colStride;
        const int y = work.top + margin + row * rowStride;
        SetWindowPos(floater, nullptr, x, y, diameter, diameter, SWP_NOACTIVATE | SWP_NOZORDER);
        ++slotIndex;
    };

    for (size_t gi = 0; gi < g_groups.size(); ++gi) {
        placeFloater(g_groups[gi].floater, dG);
        for (size_t wi = 0; wi < g_rows.size(); ++wi) {
            if (g_rows[wi].w.groupName != g_groups[gi].g.name) continue;
            if (!WidgetOccupiesLayoutSlot(g_rows[wi])) continue;
            placeFloater(g_rows[wi].floater, dW);
        }
    }

    for (size_t wi = 0; wi < g_rows.size(); ++wi) {
        const int groupIndex = FindGroupIndexByNameLocal(g_rows[wi].w.groupName);
        if (groupIndex >= 0) continue;
        if (!WidgetOccupiesLayoutSlot(g_rows[wi])) continue;
        placeFloater(g_rows[wi].floater, dW);
    }
}

void LayoutGroupFloaters() { LayoutFloatingWidgets(); }

void CreateFloaterForIndex(HINSTANCE inst, size_t i) {
    if (i >= g_rows.size()) return;
    WidgetRow& r = g_rows[i];
    if (r.floater) {
        DestroyWindow(r.floater);
        r.floater = nullptr;
    }
    const int d = FloaterDiameterPx();
    const DWORD exStyle = WS_EX_LAYERED | WS_EX_TOOLWINDOW | (r.w.alwaysVisible ? WS_EX_TOPMOST : 0);
    r.floater = CreateWindowExA(
        exStyle,
        FLOATER_CLASS_NAME,
        r.w.name.c_str(),
        WS_POPUP | WS_VISIBLE,
        0,
        0,
        d,
        d,
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

void CreateGroupFloaterForIndex(HINSTANCE inst, size_t i) {
    if (i >= g_groups.size()) return;
    GroupRow& r = g_groups[i];
    if (r.floater) {
        DestroyWindow(r.floater);
        r.floater = nullptr;
    }
    const int d = GroupFloaterDiameterPx();
    const DWORD exStyle = WS_EX_LAYERED | WS_EX_TOOLWINDOW | (r.g.alwaysVisible ? WS_EX_TOPMOST : 0);
    std::string title = r.g.name + (r.g.visible ? " [ON]" : " [OFF]");
    r.floater = CreateWindowExA(
        exStyle,
        GROUP_FLOATER_CLASS_NAME,
        title.c_str(),
        WS_POPUP | WS_VISIBLE,
        0,
        0,
        d,
        d,
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

void DestroyAllFloaters() {
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

void RebuildAllFloaters(HINSTANCE inst) {
    DestroyAllFloaters();
    for (size_t i = 0; i < g_rows.size(); ++i) CreateFloaterForIndex(inst, i);
    for (size_t i = 0; i < g_groups.size(); ++i) CreateGroupFloaterForIndex(inst, i);
    LayoutFloatingWidgets();
    LayoutGroupFloaters();
    SyncFloaterVisibility(false);
    for (size_t i = 0; i < g_rows.size(); ++i) ApplyFloaterTopmost(i);
    for (size_t i = 0; i < g_groups.size(); ++i) ApplyGroupFloaterTopmost(i);
}
