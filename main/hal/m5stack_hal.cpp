#include "hal/m5stack_hal.h"

#include <esp_log.h>
#include <device.h>
#include <led_driver.h>

#include "esp_status.h"

static const char *TAG = "m5stack_hal";

/** Full brightness, fully saturated: the status LED only ever shows flat colors. */
static constexpr int kLedBrightnessPercent = 100;
static constexpr int kLedSaturationPercent = 100;

static int hue_for(LedColor color)
{
    switch (color) {
    case LedColor::Yellow:
        return 60;
    case LedColor::Green:
        return 120;
    case LedColor::Blue:
        return 240;
    }
    return -1;
}

Result M5StackHal::init()
{
    led_driver_config_t config = led_driver_get_config();
    led_handle_ = led_driver_init(&config);
    if (led_handle_ == nullptr) {
        ESP_LOGE(TAG, "Failed to initialize LED driver");
        return Result::Failed;
    }

    return Result::Ok;
}

Result M5StackHal::i2c_select(std::uint8_t channel)
{
    // TODO: PaHub channel select once CoreS3 SE hardware is wired (ADR 01).
    (void)channel;
    return Result::NotReady;
}

Result M5StackHal::set_led(LedColor color)
{
    if (led_handle_ == nullptr) {
        return Result::NotReady;
    }

    const int hue = hue_for(color);
    if (hue < 0) {
        return Result::InvalidArgument;
    }

    auto handle = static_cast<led_driver_handle_t>(led_handle_);
    esp_err_t err = led_driver_set_hue(handle, hue);
    if (err == ESP_OK) {
        err = led_driver_set_saturation(handle, kLedSaturationPercent);
    }
    if (err == ESP_OK) {
        err = led_driver_set_brightness(handle, kLedBrightnessPercent);
    }
    if (err == ESP_OK) {
        err = led_driver_set_power(handle, true);
    }
    return from_esp_err(err);
}

Result M5StackHal::set_display_power(bool on)
{
    // TODO: drive the CoreS3 SE backlight via AXP2101 once hardware is wired.
    if (display_on_ != on) {
        display_on_ = on;
        ESP_LOGI(TAG, "Display power %s", on ? "on" : "off");
    }
    return Result::Ok;
}

Result M5StackHal::read_battery(std::uint8_t &percent)
{
    // TODO: read the AXP2101 fuel gauge once hardware is wired (UI-8, CN-7).
    (void)percent;
    return Result::NotReady;
}
