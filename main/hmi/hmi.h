#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "app_event.h"
#include "hal/hal.h"
#include "hmi/main_screen.h"
#include "hmi/status_led.h"
#include "types.h"

/**
 * Display, status LED and user input.
 *
 * Renders to the log for now; the CoreS3 SE panel replaces output_lines()
 * with M5GFX calls without changing anything above this class.
 */
class Hmi {
public:
    explicit Hmi(Hal &hal) : hal_(hal), led_(hal) {}

    Result init();

    /** Display on/off (UI-3, UI-9). */
    Result set_display_power(bool on);
    bool display_on() const { return display_on_; }

    /** Draws the main screen; a no-op while the display is off. */
    void render(const DeviceSnapshot &snapshot);

    void update_led(ConnectivityState state) { led_.update(state); }

    /** Pops one pending input event. Returns false when there is none. */
    bool poll_input(AppEvent &out);

private:
    static void button_short_press_cb(void *button_handle, void *usr_data);
    static void button_long_press_cb(void *button_handle, void *usr_data);
    void post(AppEventType type);
    void output_lines(const ScreenLine *lines, std::size_t count);

    Hal &hal_;
    StatusLed led_;
    QueueHandle_t input_queue_{nullptr};
    void *button_handle_{nullptr};
    bool display_on_{true};

    // Last content actually drawn, so an unchanged snapshot costs nothing.
    ScreenLine rendered_lines_[kScreenLineCount]{};
    std::size_t rendered_count_{0};
    bool rendered_{false};
};
