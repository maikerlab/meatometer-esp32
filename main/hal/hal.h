#pragma once

#include <cstdint>

#include "types.h"

/**
 * Board I/O contract. Owns silicon and board resources only: no temperature
 * conversion, no screen content, no Matter.
 */
class Hal {
public:
    virtual ~Hal() = default;

    virtual Result init() = 0;

    /** Route the shared sensor bus to a PaHub channel before a transaction. */
    virtual Result i2c_select(std::uint8_t channel) = 0;

    virtual Result set_led(LedColor color) = 0;

    /** Backlight / panel power (UI-3, UI-9). */
    virtual Result set_display_power(bool on) = 0;

    /** Battery charge level (UI-8, CN-7). */
    virtual Result read_battery(std::uint8_t &percent) = 0;
};
