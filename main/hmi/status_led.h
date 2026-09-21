#pragma once

#include "hal/hal.h"
#include "types.h"

/**
 * Status LED policy (UI-4), in priority order:
 * blue = commissioning, green = connected to Matter, yellow = not connected.
 */
class StatusLed {
public:
    explicit StatusLed(Hal &hal) : hal_(hal) {}

    /** Writes the LED only when the resulting color changes. */
    void update(ConnectivityState state);

private:
    Hal &hal_;
    LedColor color_{LedColor::Yellow};
    bool written_{false};
};
