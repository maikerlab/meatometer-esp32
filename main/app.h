#pragma once

#include <sdkconfig.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "app_event.h"
#include "connectivity/connectivity.h"
#include "connectivity/null_connectivity.h"
#include "hal/m5stack_hal.h"
#include "hmi/hmi.h"
#include "probes/max6675_probe.h"
#include "probes/probe_manager.h"
#include "probes/simulated_probe.h"
#include "settings/settings_store.h"
#include "types.h"

#if CONFIG_MEATOMETER_ENABLE_MATTER
#include "connectivity/matter_connectivity.h"
#endif

enum class AppMode {
    Boot,
    Running,
    DisplayOff,
    Commissioning,
    FactoryReset,
};

/**
 * Wires the modules together and runs the two tasks described in ADR 02:
 * a 1 Hz sampler that owns the probes, and a UI task that owns the display,
 * the LED and the mode state machine.
 */
class App {
public:
    App();

    /** Initializes everything, starts the tasks, then idles. Never returns. */
    void run();

private:
    static void sampler_task(void *arg);
    static void ui_task(void *arg);

    Result init();
    void sampler_loop();
    void ui_loop();
    void handle_event(const AppEvent &event);
    void enter_mode(AppMode mode);
    void perform_factory_reset();
    void note_interaction();

    M5StackHal hal_;
    SettingsStore settings_;
    ProbeManager probes_;
    Hmi hmi_;

    NullConnectivity null_connectivity_;
#if CONFIG_MEATOMETER_ENABLE_MATTER
    MatterConnectivity matter_connectivity_;
#endif
    Connectivity *connectivity_;

    Max6675Probe chamber_probe_;
    SimulatedProbe food_probe_1_;
    SimulatedProbe food_probe_2_;

    QueueHandle_t events_{nullptr};
    QueueHandle_t snapshot_mailbox_{nullptr};

    // UI task state.
    DeviceSnapshot snapshot_{};
    AppMode mode_{AppMode::Boot};
    ConnectivityState last_connectivity_{ConnectivityState::Disabled};
    TickType_t last_interaction_{0};
};
