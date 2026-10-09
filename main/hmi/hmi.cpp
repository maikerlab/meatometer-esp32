#include "hmi/hmi.h"

#include <cstdio>
#include <cstring>

#include <esp_log.h>
#include <button_gpio.h>
#include <bsp/esp-bsp.h>
#include <driver/gpio.h>
#include <iot_button.h>
#include <lvgl.h>
#include <esp_lcd_io_spi.h>

#include "esp_status.h"

static const char *TAG = "hmi";

/**
 * The CoreS3 BSP creates the panel IO without psram_dma_direct. Draw buffers
 * live in PSRAM, so each flush otherwise copies a stripe into a fresh internal
 * DMA buffer. Opening the commissioning window takes the last of that RAM and
 * the QR flush fails ("Failed to allocate priv TX buffer").
 */
extern "C" esp_err_t __real_esp_lcd_new_panel_io_spi(esp_lcd_spi_bus_handle_t bus,
                                                     const esp_lcd_panel_io_spi_config_t *io_config,
                                                     esp_lcd_panel_io_handle_t *ret_io);

extern "C" esp_err_t __wrap_esp_lcd_new_panel_io_spi(esp_lcd_spi_bus_handle_t bus,
                                                     const esp_lcd_panel_io_spi_config_t *io_config,
                                                     esp_lcd_panel_io_handle_t *ret_io)
{
    esp_lcd_panel_io_spi_config_t cfg = *io_config;
    cfg.flags.psram_dma_direct = 1;
    return __real_esp_lcd_new_panel_io_spi(bus, &cfg, ret_io);
}

/** UI-3: a press of at least this long triggers a factory reset. */
static constexpr std::uint16_t kFactoryResetPressMs = 10000;

/**
 * PORT.B PB_IN on the CoreS3 SE bottom bus (ADR 01).
 * The ESP-Matter devkit HAL button is GPIO0, which on this board is I2S MCLK
 * and reads as held. That fired this long-press about 10 s after boot.
 */
static constexpr gpio_num_t kButtonGpio = GPIO_NUM_8;

static constexpr std::size_t kInputQueueLength = 4;

/** Status bar on the 320x240 panel. The table fills whatever is left. */
static constexpr int kStatusBarHeightPx = 36;

/**
 * Give up a later redraw rather than stalling the UI task on the LVGL mutex.
 * Init waits forever: bsp_display_start() already runs the LVGL task, which holds
 * this same lock through the first 320x240 flush, often longer than 50 ms.
 * A timeout of 0 blocks indefinitely, matching the BSP examples.
 */
static constexpr std::uint32_t kDisplayLockTimeoutMs = 50;

/** Fits a quiet-zone QR and the pairing code on the 320x240 panel. */
static constexpr int32_t kQrSizePx = 180;

/** So a press on a child is delivered to the parent that posts Touch. */
static void bubble_press(lv_obj_t *obj)
{
    lv_obj_add_flag(obj, LV_OBJ_FLAG_EVENT_BUBBLE);
}

static void style_label(lv_obj_t *label, const lv_font_t *font)
{
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, font, 0);
    bubble_press(label);
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
    const button_gpio_config_t btn_gpio_cfg = {
        .gpio_num = kButtonGpio,
        .active_level = 0,
        .enable_power_save = false,
        .disable_pull = false,
    };

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

    // Draw buffers are DMA and live in PSRAM. The BSP default keeps them in
    // internal DRAM, which is the same pool the BLE controller and PSA SHA-256
    // need during commissioning. The panel IO is wrapped so SPI DMA reads
    // these buffers directly instead of bouncing them through internal RAM.
    const bsp_display_cfg_t display_cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_H_RES * CONFIG_BSP_LCD_DRAW_BUF_HEIGHT,
        .double_buffer = true,
        .flags = {
            .buff_dma = true,
            .buff_spiram = true,
            .sw_rotate = false,
        },
    };
    if (bsp_display_start_with_config(&display_cfg) == nullptr) {
        ESP_LOGE(TAG, "Failed to start CoreS3 display");
        return Result::Failed;
    }

    if (!bsp_display_lock(0)) {
        ESP_LOGE(TAG, "Failed to lock display during init");
        return Result::Failed;
    }

    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    lv_obj_t *status = lv_obj_create(screen);
    status_bar_ = status;
    lv_obj_set_size(status, LV_PCT(100), kStatusBarHeightPx);
    lv_obj_align(status, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(status, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(status, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(status, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(status, 1, 0);
    lv_obj_set_style_border_color(status, lv_palette_darken(LV_PALETTE_GREY, 2), 0);
    lv_obj_set_style_pad_hor(status, 8, 0);
    lv_obj_set_style_pad_ver(status, 4, 0);
    lv_obj_set_style_radius(status, 0, 0);
    lv_obj_set_flex_flow(status, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(status, 8, 0);
    lv_obj_set_scrollable(status, false);
    lv_obj_add_event_cb(status, [](lv_event_t *e) {
        if (lv_event_get_code(e) != LV_EVENT_PRESSED) {
            return;
        }
        static_cast<Hmi *>(lv_event_get_user_data(e))->post(AppEventType::Touch);
    }, LV_EVENT_PRESSED, this);

    lv_obj_t *battery = lv_label_create(status);
    style_label(battery, &lv_font_montserrat_12);
    lv_label_set_text(battery, "--%");
    battery_label_ = battery;

    lv_obj_t *connection = lv_label_create(status);
    style_label(connection, &lv_font_montserrat_12);
    lv_obj_set_flex_grow(connection, 1);
    lv_obj_set_style_text_align(connection, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(connection, "");
    connection_label_ = connection;

    lv_obj_t *pair = lv_button_create(status);
    pair_button_ = pair;
    lv_obj_set_style_bg_color(pair, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_set_style_pad_hor(pair, 10, 0);
    lv_obj_set_style_pad_ver(pair, 4, 0);
    lv_obj_t *pair_label = lv_label_create(pair);
    lv_label_set_text(pair_label, "Pair");
    lv_obj_set_style_text_font(pair_label, &lv_font_montserrat_12, 0);
    lv_obj_center(pair_label);
    lv_obj_add_event_cb(pair, [](lv_event_t *e) {
        // Keep the press from also counting as a screen touch on the status bar.
        lv_event_stop_bubbling(e);
        if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
            return;
        }
        auto *hmi = static_cast<Hmi *>(lv_event_get_user_data(e));
        // Backlight off: the finger only wakes the panel (UI-6, UI-9).
        if (!hmi->display_on_) {
            hmi->post(AppEventType::Touch);
            return;
        }
        hmi->post(AppEventType::PairRequested);
    }, LV_EVENT_ALL, this);

    lv_obj_t *table = lv_obj_create(screen);
    table_ = table;
    lv_obj_set_size(table, LV_PCT(100), BSP_LCD_V_RES - kStatusBarHeightPx);
    lv_obj_align(table, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(table, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(table, 0, 0);
    lv_obj_set_style_radius(table, 0, 0);
    lv_obj_set_style_pad_all(table, 8, 0);
    lv_obj_set_style_pad_row(table, 2, 0);
    lv_obj_set_flex_flow(table, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(table, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_add_event_cb(table, [](lv_event_t *e) {
        if (lv_event_get_code(e) != LV_EVENT_PRESSED) {
            return;
        }
        static_cast<Hmi *>(lv_event_get_user_data(e))->post(AppEventType::Touch);
    }, LV_EVENT_PRESSED, this);

    for (std::size_t i = 0; i < kMaxProbes; i++) {
        lv_obj_t *row = lv_obj_create(table);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 0, 0);
        lv_obj_set_style_pad_ver(row, 6, 0);
        lv_obj_set_style_pad_hor(row, 4, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_scrollable(row, false);
        bubble_press(row);
        lv_obj_set_hidden(row, true);

        lv_obj_t *name = lv_label_create(row);
        style_label(name, &lv_font_montserrat_20);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_flex_grow(name, 1);
        lv_label_set_text(name, "");

        lv_obj_t *temperature = lv_label_create(row);
        style_label(temperature, &lv_font_montserrat_20);
        lv_label_set_text(temperature, "");

        rows_[i].root = row;
        rows_[i].name = name;
        rows_[i].temperature = temperature;
    }

    lv_obj_t *pairing = lv_obj_create(screen);
    lv_obj_set_size(pairing, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(pairing, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(pairing, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(pairing, 0, 0);
    lv_obj_set_style_radius(pairing, 0, 0);
    lv_obj_set_style_pad_all(pairing, 8, 0);
    lv_obj_set_style_pad_row(pairing, 8, 0);
    lv_obj_set_flex_flow(pairing, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(pairing, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(pairing, false);
    lv_obj_set_hidden(pairing, true);
    lv_obj_add_event_cb(pairing, [](lv_event_t *e) {
        if (lv_event_get_code(e) != LV_EVENT_PRESSED) {
            return;
        }
        static_cast<Hmi *>(lv_event_get_user_data(e))->post(AppEventType::Touch);
    }, LV_EVENT_PRESSED, this);
    pairing_root_ = pairing;

    lv_obj_t *qr = lv_qrcode_create(pairing);
    lv_qrcode_set_size(qr, kQrSizePx);
    lv_qrcode_set_dark_color(qr, lv_color_black());
    lv_qrcode_set_light_color(qr, lv_color_white());
    lv_qrcode_set_quiet_zone(qr, true);
    bubble_press(qr);
    pairing_qr_ = qr;

    lv_obj_t *code = lv_label_create(pairing);
    style_label(code, &lv_font_montserrat_20);
    lv_obj_set_style_text_align(code, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(code, "");
    pairing_code_ = code;

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
        rendered_pairing_ = false;
    }
    return Result::Ok;
}

void Hmi::set_onboarding(const char *qr_payload, const char *pairing_code)
{
    snprintf(onboarding_qr_, sizeof(onboarding_qr_), "%s", qr_payload != nullptr ? qr_payload : "");
    snprintf(onboarding_code_, sizeof(onboarding_code_), "%s", pairing_code != nullptr ? pairing_code : "");
    onboarding_pending_ = true;
    // Force the next commissioning frame to encode this payload. Leaving
    // rendered_pairing_ set would skip draw_pairing forever.
    rendered_ = false;
    rendered_pairing_ = false;

    if (!display_ready_) {
        return;
    }
    if (!bsp_display_lock(kDisplayLockTimeoutMs)) {
        ESP_LOGW(TAG, "Display busy, onboarding payload kept for the next draw");
        return;
    }

    apply_onboarding_locked();
    bsp_display_unlock();
}

void Hmi::apply_onboarding_locked()
{
    auto *qr = static_cast<lv_obj_t *>(pairing_qr_);
    auto *code = static_cast<lv_obj_t *>(pairing_code_);
    if (onboarding_qr_[0] != '\0') {
        if (lv_qrcode_update(qr, onboarding_qr_, static_cast<uint32_t>(strlen(onboarding_qr_))) != LV_RESULT_OK) {
            ESP_LOGE(TAG, "Failed to encode onboarding QR");
        }
    }
    lv_label_set_text(code, onboarding_code_);
    onboarding_pending_ = false;
}

void Hmi::render(const DeviceSnapshot &snapshot, bool show_pairing)
{
    if (!display_on_) {
        return;
    }

    if (show_pairing) {
        if (rendered_ && rendered_pairing_) {
            return;
        }
        if (!draw_pairing()) {
            return;
        }
        // The widgets still don't hold this payload. Latching rendered_pairing_
        // here would skip every later encode.
        if (onboarding_pending_) {
            return;
        }
        rendered_ = true;
        rendered_pairing_ = true;
        return;
    }

    ScreenLine lines[kScreenLineCount];
    const std::size_t count = MainScreen::format(snapshot, lines, kScreenLineCount);

    bool changed = !rendered_ || rendered_pairing_ || count != rendered_count_;
    for (std::size_t i = 0; !changed && i < count; i++) {
        changed = strcmp(lines[i], rendered_lines_[i]) != 0;
    }
    if (!changed) {
        return;
    }

    if (!draw_probes(snapshot)) {
        return;
    }

    for (std::size_t i = 0; i < count; i++) {
        std::memcpy(rendered_lines_[i], lines[i], kScreenLineSize);
    }
    rendered_count_ = count;
    rendered_ = true;
    rendered_pairing_ = false;
}

bool Hmi::draw_pairing()
{
    if (!display_ready_) {
        return false;
    }
    if (!bsp_display_lock(kDisplayLockTimeoutMs)) {
        return false;
    }

    lv_obj_set_hidden(static_cast<lv_obj_t *>(status_bar_), true);
    lv_obj_set_hidden(static_cast<lv_obj_t *>(table_), true);

    if (onboarding_pending_) {
        apply_onboarding_locked();
    }

    auto *code = static_cast<lv_obj_t *>(pairing_code_);
    const char *text = lv_label_get_text(code);
    if (text == nullptr || text[0] == '\0') {
        lv_label_set_text(code, "Pairing code unavailable");
    }

    lv_obj_set_hidden(static_cast<lv_obj_t *>(pairing_root_), false);
    lv_obj_move_foreground(static_cast<lv_obj_t *>(pairing_root_));
    bsp_display_unlock();
    return true;
}

bool Hmi::draw_probes(const DeviceSnapshot &snapshot)
{
    if (!display_ready_) {
        return false;
    }
    if (!bsp_display_lock(kDisplayLockTimeoutMs)) {
        return false;
    }

    lv_obj_set_hidden(static_cast<lv_obj_t *>(pairing_root_), true);
    lv_obj_set_hidden(static_cast<lv_obj_t *>(status_bar_), false);
    lv_obj_set_hidden(static_cast<lv_obj_t *>(table_), false);

    char battery[16];
    if (snapshot.battery_valid) {
        snprintf(battery, sizeof(battery), "%u%%", snapshot.battery_percent);
    } else {
        snprintf(battery, sizeof(battery), "--%%");
    }
    lv_label_set_text(static_cast<lv_obj_t *>(battery_label_), battery);

    auto *connection = static_cast<lv_obj_t *>(connection_label_);
    if (snapshot.connectivity == ConnectivityState::Disabled) {
        lv_obj_set_hidden(connection, true);
    } else {
        const char *label = "Disconnected";
        if (!snapshot.commissioned) {
            label = "Not paired";
        } else if (snapshot.connectivity == ConnectivityState::Connected) {
            label = "Connected";
        }
        lv_label_set_text(connection, label);
        lv_obj_set_hidden(connection, false);
    }

    const bool show_pair = snapshot.connectivity != ConnectivityState::Disabled && !snapshot.commissioned;
    lv_obj_set_hidden(static_cast<lv_obj_t *>(pair_button_), !show_pair);

    const std::uint8_t shown_count =
        snapshot.probe_count < kMaxProbes ? snapshot.probe_count : static_cast<std::uint8_t>(kMaxProbes);
    for (std::uint8_t i = 0; i < shown_count; i++) {
        const ProbeReading &reading = snapshot.probes[i];
        auto *root = static_cast<lv_obj_t *>(rows_[i].root);
        auto *name = static_cast<lv_obj_t *>(rows_[i].name);
        auto *temperature = static_cast<lv_obj_t *>(rows_[i].temperature);

        char value[16];
        if (reading.connected) {
            snprintf(value, sizeof(value), "%.1f \u00B0C", reading.celsius);
        } else {
            snprintf(value, sizeof(value), "-- \u00B0C");
        }
        lv_label_set_text(name, reading.name);
        lv_label_set_text(temperature, value);
        lv_obj_set_hidden(root, false);
    }

    for (std::size_t i = shown_count; i < kMaxProbes; i++) {
        lv_obj_set_hidden(static_cast<lv_obj_t *>(rows_[i].root), true);
    }

    bsp_display_unlock();
    return true;
}
