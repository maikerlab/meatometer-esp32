#include "hmi/hmi.h"

#include <cstdio>
#include <cstring>

#include <esp_log.h>
#include <button_gpio.h>
#include <bsp/esp-bsp.h>
#include <device.h>
#include <iot_button.h>
#include <lvgl.h>

#include "esp_status.h"

static const char *TAG = "hmi";

/** UI-3: a press of at least this long triggers a factory reset. */
static constexpr std::uint16_t kFactoryResetPressMs = 10000;

static constexpr std::size_t kInputQueueLength = 4;

/** Diameter of one probe circle on the 320x240 panel, leaving a gap between three of them. */
static constexpr int kCircleDiameterPx = 96;
static constexpr int kCircleBorderPx = 3;
static constexpr int kCircleGapPx = 8;

/** Give up the redraw rather than stalling the UI task on the LVGL mutex. */
static constexpr std::uint32_t kDisplayLockTimeoutMs = 50;

static lv_obj_t *make_circle(lv_obj_t *parent, lv_obj_t **name_label, lv_obj_t **temperature_label)
{
    lv_obj_t *circle = lv_obj_create(parent);
    lv_obj_set_size(circle, kCircleDiameterPx, kCircleDiameterPx);
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(circle, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_set_style_border_width(circle, kCircleBorderPx, 0);
    lv_obj_set_style_bg_color(circle, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(circle, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(circle, 6, 0);
    lv_obj_set_style_text_color(circle, lv_color_white(), 0);
    lv_obj_set_flex_flow(circle, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(circle, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(circle, false);

    *name_label = lv_label_create(circle);
    lv_obj_set_style_text_font(*name_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(*name_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(*name_label, kCircleDiameterPx - 16);
    lv_label_set_long_mode(*name_label, LV_LABEL_LONG_DOT);
    lv_label_set_text(*name_label, "");

    *temperature_label = lv_label_create(circle);
    lv_obj_set_style_text_font(*temperature_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_align(*temperature_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(*temperature_label, "");

    return circle;
}

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

    if (bsp_display_start() == nullptr) {
        ESP_LOGE(TAG, "Failed to start CoreS3 display");
        return Result::Failed;
    }

    if (!bsp_display_lock(kDisplayLockTimeoutMs)) {
        ESP_LOGE(TAG, "Failed to lock display during init");
        return Result::Failed;
    }

    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    lv_obj_t *row = lv_obj_create(screen);
    lv_obj_set_size(row, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, kCircleGapPx, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(row, false);

    lv_obj_t *empty = lv_label_create(screen);
    lv_obj_set_style_text_color(empty, lv_color_white(), 0);
    lv_obj_set_style_text_font(empty, &lv_font_montserrat_12, 0);
    lv_label_set_text(empty, "No probes connected");
    lv_obj_center(empty);
    lv_obj_set_hidden(empty, true);
    empty_label_ = empty;

    for (std::size_t i = 0; i < kProbeCircleCount; i++) {
        lv_obj_t *name = nullptr;
        lv_obj_t *temperature = nullptr;
        circles_[i].root = make_circle(row, &name, &temperature);
        circles_[i].name = name;
        circles_[i].temperature = temperature;
        lv_obj_set_hidden(static_cast<lv_obj_t *>(circles_[i].root), true);
    }

    bsp_display_unlock();
    display_ready_ = true;
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

    if (!draw_probes(snapshot, lines, count)) {
        return;
    }

    for (std::size_t i = 0; i < count; i++) {
        std::memcpy(rendered_lines_[i], lines[i], kScreenLineSize);
    }
    rendered_count_ = count;
    rendered_ = true;
}

bool Hmi::draw_probes(const DeviceSnapshot &snapshot, const ScreenLine *lines, std::size_t count)
{
    if (!display_ready_) {
        return false;
    }
    if (!bsp_display_lock(kDisplayLockTimeoutMs)) {
        return false;
    }

    std::size_t shown = 0;
    for (std::uint8_t i = 0; i < snapshot.probe_count && shown < kProbeCircleCount; i++) {
        const ProbeReading &reading = snapshot.probes[i];
        if (!reading.connected) {
            continue;
        }

        auto *root = static_cast<lv_obj_t *>(circles_[shown].root);
        auto *name = static_cast<lv_obj_t *>(circles_[shown].name);
        auto *temperature = static_cast<lv_obj_t *>(circles_[shown].temperature);

        char value[16];
        snprintf(value, sizeof(value), "%.1f\u00B0C", reading.celsius);
        lv_label_set_text(name, reading.name);
        lv_label_set_text(temperature, value);
        lv_obj_set_hidden(root, false);
        shown++;
    }

    for (std::size_t i = shown; i < kProbeCircleCount; i++) {
        lv_obj_set_hidden(static_cast<lv_obj_t *>(circles_[i].root), true);
    }

    auto *empty = static_cast<lv_obj_t *>(empty_label_);
    if (shown == 0) {
        lv_label_set_text(empty, count > 0 ? lines[0] : "No probes connected");
        lv_obj_set_hidden(empty, false);
    } else {
        lv_obj_set_hidden(empty, true);
    }

    bsp_display_unlock();
    return true;
}
