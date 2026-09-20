#pragma once

#include "hal.h"

class EspIdfHal : public Hal {
public:
    HalStatus init() override;
    HalStatus set_led(LedColor color) override;
    HalStatus read_temperature(float &celsius) override;

    HalStatus set_power(bool on) override;
    HalStatus set_brightness(int percent) override;
    HalStatus set_hue(int degrees) override;
    HalStatus set_saturation(int percent) override;
    HalStatus set_color_temperature(std::uint32_t kelvin_factor) override;
    HalStatus set_xy(std::uint16_t x, std::uint16_t y) override;

private:
    void *led_handle_{nullptr};
};
