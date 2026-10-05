#include "hal/m5stack_hal.h"

#include <esp_log.h>
#include <bsp/esp-bsp.h>
#include <bsp/m5stack_core_s3.h>
#include <esp_io_expander.h>
#include <led_strip.h>

#include "esp_status.h"

static const char *TAG = "m5stack_hal";

/** M5GO Battery Bottom3 ring: 10 WS2812s, data on bottom-bus G5 (ADR 01). */
static constexpr int kRingGpio = 5;
static constexpr uint32_t kRingLedCount = 10;

/** ~40% so the whole ring is visible without a full-white current spike. */
static constexpr uint8_t kRingValue = 102;
static constexpr uint8_t kRingSaturation = 255;

static int hue_for(LedColor color)
{
    switch (color) {
    case LedColor::Yellow:
        return 60;
    case LedColor::Green:
        return 120;
    case LedColor::Blue:
        return 240;
    case LedColor::Off:
        return -1;
    }
    return -1;
}

/**
 * CoreS3 / CoreS3 SE: Grove and M-Bus 5 V are off until the AW9523B enables
 * the SY7088 boost and the bus-output switch (M5Unified setExtOutput(true)).
 * On battery that is the only 5 V source for Ports A/B/C.
 *
 * AW9523 P0_1 = BUS_OUT_EN, P0_5 = USB_OTG_EN, P1_7 = BOOST_EN.
 */
static Result enable_grove_bus_power()
{
    constexpr uint32_t kBusOutEn = IO_EXPANDER_PIN_NUM_1;
    constexpr uint32_t kUsbOtgEn = BSP_USB_EN;
    constexpr uint32_t kBoostEn = IO_EXPANDER_PIN_NUM_15;

    if (bsp_i2c_init() != ESP_OK) {
        ESP_LOGE(TAG, "Internal I2C init failed");
        return Result::Failed;
    }

    esp_io_expander_handle_t io = bsp_io_expander_init();
    if (io == nullptr) {
        ESP_LOGE(TAG, "AW9523B init failed");
        return Result::Failed;
    }

    esp_err_t err = esp_io_expander_set_dir(io, kBusOutEn | kUsbOtgEn | kBoostEn, IO_EXPANDER_OUTPUT);
    if (err == ESP_OK) {
        err = esp_io_expander_set_level(io, kBoostEn, 1);
    }
    if (err == ESP_OK) {
        err = esp_io_expander_set_level(io, kBusOutEn, 1);
    }
    if (err == ESP_OK) {
        err = esp_io_expander_set_level(io, kUsbOtgEn, 0);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable Grove 5V: %s", esp_err_to_name(err));
        return from_esp_err(err);
    }

    ESP_LOGI(TAG, "Grove/M-Bus 5V enabled (BOOST_EN + BUS_OUT_EN)");
    return Result::Ok;
}

Result M5StackHal::init()
{
    if (enable_grove_bus_power() != Result::Ok) {
        return Result::Failed;
    }

    led_strip_config_t strip_config = {};
    strip_config.strip_gpio_num = kRingGpio;
    strip_config.max_leds = kRingLedCount;
    strip_config.led_model = LED_MODEL_WS2812;
    strip_config.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;

    led_strip_rmt_config_t rmt_config = {};
    rmt_config.clk_src = RMT_CLK_SRC_DEFAULT;
    rmt_config.resolution_hz = 10 * 1000 * 1000;

    led_strip_handle_t strip = nullptr;
    const esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &strip);
    if (err != ESP_OK || strip == nullptr) {
        ESP_LOGE(TAG, "Failed to initialize LED ring: %s", esp_err_to_name(err));
        return from_esp_err(err);
    }

    led_handle_ = strip;
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

    auto strip = static_cast<led_strip_handle_t>(led_handle_);
    if (color == LedColor::Off) {
        return from_esp_err(led_strip_clear(strip));
    }

    const int hue = hue_for(color);
    if (hue < 0) {
        return Result::InvalidArgument;
    }

    esp_err_t err = ESP_OK;
    for (uint32_t i = 0; i < kRingLedCount && err == ESP_OK; i++) {
        err = led_strip_set_pixel_hsv(strip, i, static_cast<uint16_t>(hue), kRingSaturation, kRingValue);
    }
    if (err == ESP_OK) {
        err = led_strip_refresh(strip);
    }
    return from_esp_err(err);
}

Result M5StackHal::set_display_power(bool on)
{
    const esp_err_t err = on ? bsp_display_backlight_on() : bsp_display_backlight_off();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set backlight: %s", esp_err_to_name(err));
        return from_esp_err(err);
    }

    display_on_ = on;
    return Result::Ok;
}

Result M5StackHal::read_battery(std::uint8_t &percent)
{
    // TODO: read the AXP2101 fuel gauge once hardware is wired (UI-8, CN-7).
    (void)percent;
    return Result::NotReady;
}
