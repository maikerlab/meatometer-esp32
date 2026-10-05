#pragma once

#include "hal/hal.h"
#include "types.h"

/**
 * Status LED policy (UI-4), in priority order:
 * blue = commissioning, green = connected to Matter, yellow = not connected.
 *
 * An active Matter Identify overrides that color with a 2 Hz blue flash, then
 * the steady color is written again.
 */
class StatusLed {
public:
    explicit StatusLed(Hal &hal) : hal_(hal) {}

    /** Writes the LED only when the resulting color changes. */
    void update(ConnectivityState state);

    /** Starts or ends the identify flash. Ending restores the steady color. */
    void set_identify(bool active);

    /** Advances the 2 Hz flash. No effect while identify is inactive. */
    void tick();

private:
    static constexpr std::uint32_t kIdentifyHalfPeriodMs = 250;

    Hal &hal_;
    LedColor color_{LedColor::Yellow};
    bool written_{false};
    bool identifying_{false};
    bool phase_on_{true};
    std::uint32_t last_toggle_{0};
};
