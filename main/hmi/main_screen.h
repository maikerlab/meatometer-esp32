#pragma once

#include <cstddef>

#include "types.h"

/** Characters per rendered line, including the terminator. */
inline constexpr std::size_t kScreenLineSize = 48;

/**
 * Lines the main screen compares to skip unchanged redraws:
 * one status line plus one line per probe (UI-2).
 */
inline constexpr std::size_t kScreenLineCount = kMaxProbes + 1;

using ScreenLine = char[kScreenLineSize];

/**
 * Home-screen content (UI-2): status bar, then every configured probe.
 * A probe with no valid reading is still listed.
 *
 * Pure formatting, no hardware. The LVGL widgets read the snapshot; this
 * text is only the redraw check.
 */
class MainScreen {
public:
    /** Fills `lines` and returns how many were written. Line 0 is the status bar. */
    static std::size_t format(const DeviceSnapshot &snapshot, ScreenLine *lines, std::size_t max_lines);
};
