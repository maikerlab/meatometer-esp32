#include "hmi/status_led.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static LedColor color_for(ConnectivityState state)
{
    switch (state) {
    case ConnectivityState::Commissioning:
        return LedColor::Blue;
    case ConnectivityState::Connected:
        return LedColor::Green;
    case ConnectivityState::Disconnected:
    case ConnectivityState::Disabled:
    default:
        return LedColor::Yellow;
    }
}

void StatusLed::update(ConnectivityState state)
{
    const LedColor color = color_for(state);
    if (identifying_) {
        color_ = color;
        return;
    }

    if (written_ && color == color_) {
        return;
    }

    if (hal_.set_led(color) == Result::Ok) {
        color_ = color;
        written_ = true;
    }
}

void StatusLed::set_identify(bool active)
{
    if (active == identifying_) {
        return;
    }

    identifying_ = active;
    if (active) {
        phase_on_ = true;
        last_toggle_ = static_cast<std::uint32_t>(xTaskGetTickCount());
        hal_.set_led(LedColor::Blue);
        return;
    }

    written_ = false;
    if (hal_.set_led(color_) == Result::Ok) {
        written_ = true;
    }
}

void StatusLed::tick()
{
    if (!identifying_) {
        return;
    }

    const auto now = static_cast<std::uint32_t>(xTaskGetTickCount());
    if (static_cast<std::uint32_t>(pdTICKS_TO_MS(now - last_toggle_)) < kIdentifyHalfPeriodMs) {
        return;
    }

    last_toggle_ = now;
    phase_on_ = !phase_on_;
    hal_.set_led(phase_on_ ? LedColor::Blue : LedColor::Off);
}
