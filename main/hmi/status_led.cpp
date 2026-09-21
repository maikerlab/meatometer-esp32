#include "hmi/status_led.h"

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
    if (written_ && color == color_) {
        return;
    }

    if (hal_.set_led(color) == Result::Ok) {
        color_ = color;
        written_ = true;
    }
}
