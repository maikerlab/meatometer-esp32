#pragma once

#include "hal/hal.h"

/**
 * CoreS3 SE + M5GO Battery Bottom3 + Unit PaHub (see ADR 01).
 *
 * Display backlight goes through the CoreS3 BSP (AXP2101). The PaHub and
 * battery gauge are stubbed until that hardware is wired. The LED path works
 * on any esp-matter device_hal board.
 */
class M5StackHal : public Hal {
public:
    Result init() override;
    Result i2c_select(std::uint8_t channel) override;
    Result set_led(LedColor color) override;
    Result set_display_power(bool on) override;
    Result read_battery(std::uint8_t &percent) override;

private:
    void *led_handle_{nullptr};
    bool display_on_{true};
};
