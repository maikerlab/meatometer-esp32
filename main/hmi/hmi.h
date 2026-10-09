#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "app_event.h"
#include "connectivity/connectivity.h"
#include "hal/hal.h"
#include "hmi/main_screen.h"
#include "hmi/status_led.h"
#include "types.h"

/**
 * Display, status LED and user input.
 *
 * The CoreS3 SE panel is drawn with LVGL through the M5Stack CoreS3 BSP.
 * Nothing above this class knows about the panel.
 */
class Hmi {
public:
    explicit Hmi(Hal &hal) : hal_(hal), led_(hal) {}

    Result init();

    /** Display on/off (UI-3, UI-9). */
    Result set_display_power(bool on);
    bool display_on() const { return display_on_; }

    /**
     * Draws the sensor table, or the pairing QR when `show_pairing` is set.
     * A no-op while the display is off.
     */
    void render(const DeviceSnapshot &snapshot, bool show_pairing);

    /** BLE onboarding QR payload and the manual pairing code shown beneath it. */
    void set_onboarding(const char *qr_payload, const char *pairing_code);

    void update_led(ConnectivityState state) { led_.update(state); }

    /** Matter Identify flash. The UI task calls both; the LED is not touched elsewhere. */
    void set_identify(bool active) { led_.set_identify(active); }
    void tick_led() { led_.tick(); }

    /** Pops one pending input event. Returns false when there is none. */
    bool poll_input(AppEvent &out);

private:
    static void button_short_press_cb(void *button_handle, void *usr_data);
    static void button_long_press_cb(void *button_handle, void *usr_data);
    void post(AppEventType type);
    /** Pushes the snapshot onto the table. False if the panel lock was busy. */
    bool draw_probes(const DeviceSnapshot &snapshot);
    /** QR plus pairing code. False if the panel lock was busy. */
    bool draw_pairing();
    /** Writes the stored onboarding payload into the widgets. Caller holds the display lock. */
    void apply_onboarding_locked();

    Hal &hal_;
    StatusLed led_;
    QueueHandle_t input_queue_{nullptr};
    void *button_handle_{nullptr};
    bool display_on_{true};

    /** One table row: name and temperature of a configured probe. */
    struct ProbeRow {
        void *root;
        void *name;
        void *temperature;
    };

    void *status_bar_{nullptr};
    void *battery_label_{nullptr};
    void *connection_label_{nullptr};
    void *pair_button_{nullptr};
    void *table_{nullptr};
    void *pairing_root_{nullptr};
    void *pairing_qr_{nullptr};
    void *pairing_code_{nullptr};
    ProbeRow rows_[kMaxProbes]{};
    bool display_ready_{false};

    // Last content actually drawn, so an unchanged snapshot costs nothing.
    ScreenLine rendered_lines_[kScreenLineCount]{};
    std::size_t rendered_count_{0};
    bool rendered_{false};
    bool rendered_pairing_{false};

    /**
     * Last onboarding payload from connectivity. A missed display lock must not
     * discard it: the pairing screen is drawn later, and that draw is what
     * latches rendered_pairing_.
     */
    char onboarding_qr_[kOnboardingPayloadSize]{};
    char onboarding_code_[kManualPairingCodeSize]{};
    bool onboarding_pending_{false};
};
