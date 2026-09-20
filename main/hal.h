#pragma once

#include <cstdint>

enum class HalStatus {
    Ok,
    Failed,
    InvalidArgument,
    NotReady,
};

enum class LedColor {
    Yellow,
    Green,
};

class Hal {
public:
    virtual ~Hal() = default;

    virtual HalStatus init() = 0;
    virtual HalStatus set_led(LedColor color) = 0;
    virtual HalStatus read_temperature(float &celsius) = 0;

    virtual HalStatus set_power(bool on) = 0;
    virtual HalStatus set_brightness(int percent) = 0;
    virtual HalStatus set_hue(int degrees) = 0;
    virtual HalStatus set_saturation(int percent) = 0;
    virtual HalStatus set_color_temperature(std::uint32_t kelvin_factor) = 0;
    virtual HalStatus set_xy(std::uint16_t x, std::uint16_t y) = 0;
};
