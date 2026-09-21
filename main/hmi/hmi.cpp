#include "hmi/hmi.h"

#include <cstring>

#include <esp_log.h>
#include <button_gpio.h>
#include <device.h>
#include <iot_button.h>

#include "esp_status.h"

static const char *TAG = "hmi";

/** UI-3: a press of at least this long triggers a factory reset. */
static constexpr std::uint16_t kFactoryResetPressMs = 10000;

static constexpr std::size_t kInputQueueLength = 4;

Result Hmi::init()
{
    input_queue_ = xQueueCreate(kInputQueueLength, sizeof(AppEvent));
    if (input_queue_ == nullptr) {
        return Result::Failed;
    }

    button_handle_t handle = nullptr;
    const button_config_t btn_cfg = {
        .long_press_time = kFactoryResetPressMs,
        .short_press_time = 0,
    };
    const button_gpio_config_t btn_gpio_cfg = button_driver_get_config();

    esp_err_t err = iot_button_new_gpio_device(&btn_cfg, &btn_gpio_cfg, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create button device: %s", esp_err_to_name(err));
        return from_esp_err(err);
    }

    // Both callbacks run on the button component's timer task: post and return.
    err = iot_button_register_cb(handle, BUTTON_SINGLE_CLICK, nullptr, button_short_press_cb, this);
    if (err == ESP_OK) {
        err = iot_button_register_cb(handle, BUTTON_LONG_PRESS_START, nullptr, button_long_press_cb, this);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register button callbacks: %s", esp_err_to_name(err));
        return from_esp_err(err);
    }

    button_handle_ = handle;
    return Result::Ok;
}

void Hmi::button_short_press_cb(void *button_handle, void *usr_data)
{
    (void)button_handle;
    static_cast<Hmi *>(usr_data)->post(AppEventType::ButtonShort);
}

void Hmi::button_long_press_cb(void *button_handle, void *usr_data)
{
    (void)button_handle;
    static_cast<Hmi *>(usr_data)->post(AppEventType::ButtonLongReset);
}

void Hmi::post(AppEventType type)
{
    const AppEvent event = {type};
    xQueueSend(input_queue_, &event, 0);
}

bool Hmi::poll_input(AppEvent &out)
{
    if (input_queue_ == nullptr) {
        return false;
    }

    // TODO: poll the CoreS3 SE touch panel here and post AppEventType::Touch.
    return xQueueReceive(input_queue_, &out, 0) == pdTRUE;
}

Result Hmi::set_display_power(bool on)
{
    const Result status = hal_.set_display_power(on);
    if (status != Result::Ok) {
        return status;
    }

    display_on_ = on;
    if (!on) {
        rendered_ = false;
    }
    return Result::Ok;
}

void Hmi::render(const DeviceSnapshot &snapshot)
{
    if (!display_on_) {
        return;
    }

    ScreenLine lines[kScreenLineCount];
    const std::size_t count = MainScreen::format(snapshot, lines, kScreenLineCount);

    bool changed = !rendered_ || count != rendered_count_;
    for (std::size_t i = 0; !changed && i < count; i++) {
        changed = strcmp(lines[i], rendered_lines_[i]) != 0;
    }
    if (!changed) {
        return;
    }

    output_lines(lines, count);

    for (std::size_t i = 0; i < count; i++) {
        snprintf(rendered_lines_[i], kScreenLineSize, "%s", lines[i]);
    }
    rendered_count_ = count;
    rendered_ = true;
}

void Hmi::output_lines(const ScreenLine *lines, std::size_t count)
{
    // TODO: draw with M5GFX once the CoreS3 SE panel is wired (UI-1).
    for (std::size_t i = 0; i < count; i++) {
        ESP_LOGI(TAG, "%s", lines[i]);
    }
}
