#pragma once

#include <algorithm>

namespace motion {

// Minimal Pro v1: centralized motion tokens for Win32 animation timing.
// Easing curves are documented for future non-linear tweening work.
struct Tokens {
    // Durations (ms)
    static constexpr unsigned int kFastMs = 120;    // press feedback
    static constexpr unsigned int kNormalMs = 180;  // hover/list transitions
    static constexpr unsigned int kPanelMs = 240;   // panel open/close

    // Distances (px)
    static constexpr int kShiftSmallPx = 4;
    static constexpr int kShiftMediumPx = 8;

    // Scale factors (percent)
    static constexpr int kHoverScalePct = 102;
    static constexpr int kPressScalePct = 98;

    // Canonical easing (for docs / future tween function)
    // enter: cubic-bezier(0.16, 1, 0.3, 1)
    // exit:  cubic-bezier(0.7, 0, 0.84, 0)
};

// Converts desired animation duration into timer ticks.
constexpr unsigned int DurationToTicks(unsigned int durationMs, unsigned int tickIntervalMs) {
    if (tickIntervalMs == 0) return 1;
    return std::max(1u, (durationMs + tickIntervalMs - 1) / tickIntervalMs);
}

// Computes per-tick alpha increment for fade animations.
constexpr unsigned char AlphaStep(unsigned int startAlpha, unsigned int endAlpha, unsigned int ticks) {
    if (ticks == 0) return static_cast<unsigned char>(endAlpha > startAlpha ? (endAlpha - startAlpha) : (startAlpha - endAlpha));
    const unsigned int delta = (endAlpha > startAlpha) ? (endAlpha - startAlpha) : (startAlpha - endAlpha);
    const unsigned int step = std::max(1u, (delta + ticks - 1) / ticks);
    return static_cast<unsigned char>(std::min(step, 255u));
}

} // namespace motion
