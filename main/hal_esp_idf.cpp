#include "hal_esp_idf.h"

#include <driver/gpio.h>
#include <esp_err.h>
#include <esp_log.h>
#include <device.h>
#include <led_driver.h>
#include <max6675.h>

static const char *TAG = "hal_esp_idf";

static HalStatus from_esp_err(esp_err_t err)
{
    switch (err) {
    case ESP_OK:
        return HalStatus::Ok;
    case ESP_ERR_INVALID_ARG:
        return HalStatus::InvalidArgument;
    case ESP_ERR_INVALID_STATE:
        return HalStatus::NotReady;
    default:
        return HalStatus::Failed;
    }
}

HalStatus EspIdfHal::init()
{
    MAX6675_structure max6675_cfg = {
        .MAX6675_SCK = GPIO_NUM_0,
        .MAX6675_CS = GPIO_NUM_1,
        .MAX6675_MISO = GPIO_NUM_2,
        .TEMPERATURE_CALIBRATION_COEFFICIENT = 0.25f,
    };
    MAX6675_init(max6675_cfg);

    led_driver_config_t config = led_driver_get_config();
    led_handle_ = led_driver_init(&config);
    if (led_handle_ == nullptr) {
        ESP_LOGE(TAG, "Failed to initialize LED driver");
        return HalStatus::Failed;
    }

    return HalStatus::Ok;
}

HalStatus EspIdfHal::set_led(LedColor color)
{
    int hue = 0;
    switch (color) {
    case LedColor::Yellow:
        hue = 60;
        break;
    case LedColor::Green:
        hue = 120;
        break;
    default:
        return HalStatus::InvalidArgument;
    }

    HalStatus status = set_hue(hue);
    if (status != HalStatus::Ok) {
        return status;
    }
    status = set_saturation(100);
    if (status != HalStatus::Ok) {
        return status;
    }
    status = set_brightness(100);
    if (status != HalStatus::Ok) {
        return status;
    }
    return set_power(true);
}

HalStatus EspIdfHal::read_temperature(float &celsius)
{
    celsius = static_cast<float>(readCelsius());
    return HalStatus::Ok;
}

HalStatus EspIdfHal::set_power(bool on)
{
    if (led_handle_ == nullptr) {
        return HalStatus::NotReady;
    }
    return from_esp_err(led_driver_set_power(static_cast<led_driver_handle_t>(led_handle_), on));
}

HalStatus EspIdfHal::set_brightness(int percent)
{
    if (led_handle_ == nullptr) {
        return HalStatus::NotReady;
    }
    return from_esp_err(led_driver_set_brightness(static_cast<led_driver_handle_t>(led_handle_), percent));
}

HalStatus EspIdfHal::set_hue(int degrees)
{
    if (led_handle_ == nullptr) {
        return HalStatus::NotReady;
    }
    return from_esp_err(led_driver_set_hue(static_cast<led_driver_handle_t>(led_handle_), degrees));
}

HalStatus EspIdfHal::set_saturation(int percent)
{
    if (led_handle_ == nullptr) {
        return HalStatus::NotReady;
    }
    return from_esp_err(led_driver_set_saturation(static_cast<led_driver_handle_t>(led_handle_), percent));
}

HalStatus EspIdfHal::set_color_temperature(std::uint32_t kelvin_factor)
{
    if (led_handle_ == nullptr) {
        return HalStatus::NotReady;
    }
    return from_esp_err(led_driver_set_temperature(static_cast<led_driver_handle_t>(led_handle_), kelvin_factor));
}

HalStatus EspIdfHal::set_xy(std::uint16_t x, std::uint16_t y)
{
    if (led_handle_ == nullptr) {
        return HalStatus::NotReady;
    }
    return from_esp_err(led_driver_set_xy(static_cast<led_driver_handle_t>(led_handle_), x, y));
}
