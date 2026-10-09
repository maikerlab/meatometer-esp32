#include "hmi/main_screen.h"

#include <cstdio>

namespace {

const char *connection_label(const DeviceSnapshot &snapshot)
{
    if (snapshot.connectivity == ConnectivityState::Disabled) {
        return "";
    }
    if (!snapshot.commissioned) {
        return "Not paired";
    }
    if (snapshot.connectivity == ConnectivityState::Connected) {
        return "Connected";
    }
    return "Disconnected";
}

} // namespace

std::size_t MainScreen::format(const DeviceSnapshot &snapshot, ScreenLine *lines, std::size_t max_lines)
{
    if (max_lines == 0) {
        return 0;
    }

    char battery[16];
    if (snapshot.battery_valid) {
        snprintf(battery, sizeof(battery), "%u%%", snapshot.battery_percent);
    } else {
        snprintf(battery, sizeof(battery), "--%%");
    }

    const bool show_pair = snapshot.connectivity != ConnectivityState::Disabled && !snapshot.commissioned;
    snprintf(lines[0], kScreenLineSize, "%s|%s|%d", battery, connection_label(snapshot), show_pair ? 1 : 0);

    std::size_t count = 1;
    for (std::uint8_t i = 0; i < snapshot.probe_count && count < max_lines; i++) {
        const ProbeReading &reading = snapshot.probes[i];
        if (reading.connected) {
            snprintf(lines[count], kScreenLineSize, "%s  %.1f \u00B0C", reading.name, reading.celsius);
        } else {
            snprintf(lines[count], kScreenLineSize, "%s  -- \u00B0C", reading.name);
        }
        count++;
    }

    return count;
}
