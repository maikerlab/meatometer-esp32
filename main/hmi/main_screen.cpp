#include "hmi/main_screen.h"

#include <cstdio>

std::size_t MainScreen::format(const DeviceSnapshot &snapshot, ScreenLine *lines, std::size_t max_lines)
{
    std::size_t count = 0;

    for (std::uint8_t i = 0; i < snapshot.probe_count && count < max_lines; i++) {
        const ProbeReading &reading = snapshot.probes[i];
        if (!reading.connected) {
            continue;
        }
        snprintf(lines[count], kScreenLineSize, "%-12s %6.1f C", reading.name, reading.celsius);
        count++;
    }

    if (count == 0 && max_lines > 0) {
        snprintf(lines[count], kScreenLineSize, "No probes connected");
        count++;
    }

    if (snapshot.battery_valid && count < max_lines) {
        snprintf(lines[count], kScreenLineSize, "Battery %u%%", snapshot.battery_percent);
        count++;
    }

    return count;
}
