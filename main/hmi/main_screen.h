#pragma once

#include <cstddef>

#include "types.h"

/** Characters per rendered line, including the terminator. */
inline constexpr std::size_t kScreenLineSize = 48;

/** Lines the main screen can show: one per probe plus a battery line. */
inline constexpr std::size_t kScreenLineCount = kMaxProbes + 1;

using ScreenLine = char[kScreenLineSize];

/**
 * Main screen content (UI-2): name and temperature of every connected probe.
 * Disconnected probes are not listed.
 *
 * Pure formatting, no hardware. The LVGL circles use the readings directly;
 * this text is the empty-state line and the redraw check.
 */
class MainScreen {
public:
    /** Fills `lines` and returns how many were written. */
    static std::size_t format(const DeviceSnapshot &snapshot, ScreenLine *lines, std::size_t max_lines);
};
