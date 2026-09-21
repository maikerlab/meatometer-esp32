#include "app.h"

#include <cinttypes>

#include <freertos/task.h>

#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>

#include <log_heap_numbers.h>

static const char *TAG = "app";

/** PR-6: every connected probe is sampled once per second. */
static constexpr std::uint32_t kSampleIntervalMs = 1000;

/** A cycle taking longer than this puts PR-6 at risk; say so. */
static constexpr std::uint32_t kSampleBudgetMs = 800;

/** UI task wakeup interval: bounds input latency without busy-looping. */
static constexpr std::uint32_t kUiTickMs = 50;

/** UI-9: the display turns off after 5 minutes without interaction. */
static constexpr std::uint32_t kIdleTimeoutMs = 5 * 60 * 1000;

static constexpr std::uint32_t kHeapStatIntervalMs = 10000;

static constexpr std::size_t kEventQueueLength = 8;
static constexpr std::uint32_t kSamplerStackSize = 4096;
static constexpr std::uint32_t kUiStackSize = 4096;
static constexpr UBaseType_t kSamplerPriority = 3;
static constexpr UBaseType_t kUiPriority = 2;

/** Chamber probe wiring, see ADR 01. Replaced by KmeterProbe on I2C later. */
static constexpr Max6675Probe::Config kChamberProbeConfig = {
    .sck_gpio = 0,
    .cs_gpio = 1,
    .miso_gpio = 2,
    .calibration_coefficient = 0.25f,
};

/** Placeholder food probes until the ADS1115 + NTC path is wired (PR-1). */
static constexpr SimulatedProbe::Config kFoodProbeConfig = {
    .min_celsius = 20.0f,
    .max_celsius = 95.0f,
    .steps_per_ramp = 150,
};

App::App()
    : probes_(settings_), hmi_(hal_),
#if CONFIG_MEATOMETER_ENABLE_MATTER
      connectivity_(&matter_connectivity_),
#else
      connectivity_(&null_connectivity_),
#endif
      chamber_probe_(0, kChamberProbeConfig), food_probe_1_(1, ProbeKind::Food, kFoodProbeConfig),
      food_probe_2_(2, ProbeKind::Food, kFoodProbeConfig)
{
}

Result App::init()
{
    nvs_flash_init();

    MEMORY_PROFILER_DUMP_HEAP_STAT("Bootup");

    if (hal_.init() != Result::Ok) {
        ESP_LOGE(TAG, "Failed to initialize HAL");
        return Result::Failed;
    }

    if (settings_.init() != Result::Ok) {
        ESP_LOGE(TAG, "Failed to initialize settings");
        return Result::Failed;
    }

    if (hmi_.init() != Result::Ok) {
        ESP_LOGE(TAG, "Failed to initialize HMI");
        return Result::Failed;
    }
    hmi_.update_led(connectivity_->state());

    probes_.add(&chamber_probe_);
    probes_.add(&food_probe_1_);
    probes_.add(&food_probe_2_);
    if (probes_.init() != Result::Ok) {
        // A probe that fails to initialize just reports as disconnected
        // (PR-5); the other probes keep working.
        ESP_LOGW(TAG, "One or more probes failed to initialize");
    }

    if (probes_.conversion_time_ms() >= kSampleBudgetMs) {
        ESP_LOGW(TAG, "Conversion time %" PRIu32 " ms leaves little room in a %" PRIu32 " ms cycle",
                 probes_.conversion_time_ms(), kSampleIntervalMs);
    }

    events_ = xQueueCreate(kEventQueueLength, sizeof(AppEvent));
    snapshot_mailbox_ = xQueueCreate(1, sizeof(DeviceSnapshot));
    if (events_ == nullptr || snapshot_mailbox_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create queues");
        return Result::Failed;
    }

    if (connectivity_->start(probes_.probe_count()) != Result::Ok) {
        ESP_LOGE(TAG, "Failed to start connectivity");
        return Result::Failed;
    }
    last_connectivity_ = connectivity_->state();
    hmi_.update_led(last_connectivity_);

    return Result::Ok;
}

void App::run()
{
    if (init() != Result::Ok) {
        ESP_LOGE(TAG, "Initialization failed, restarting");
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    }

    enter_mode(AppMode::Running);

    xTaskCreate(sampler_task, "sampler", kSamplerStackSize, this, kSamplerPriority, nullptr);
    xTaskCreate(ui_task, "ui", kUiStackSize, this, kUiPriority, nullptr);

    while (true) {
        MEMORY_PROFILER_DUMP_HEAP_STAT("Idle");
        vTaskDelay(pdMS_TO_TICKS(kHeapStatIntervalMs));
    }
}

void App::sampler_task(void *arg)
{
    static_cast<App *>(arg)->sampler_loop();
}

void App::ui_task(void *arg)
{
    static_cast<App *>(arg)->ui_loop();
}

void App::sampler_loop()
{
    DeviceSnapshot snapshot = {};
    TickType_t last_wake = xTaskGetTickCount();

    while (true) {
        const TickType_t started = xTaskGetTickCount();

        snapshot.connectivity = connectivity_->state();
        snapshot.battery_percent = 0;
        snapshot.battery_valid = hal_.read_battery(snapshot.battery_percent) == Result::Ok;

        probes_.sample(snapshot);

        // Returns immediately; the write happens on the CHIP event loop.
        connectivity_->publish(snapshot);

        // Newest snapshot wins: a slow consumer drops frames instead of
        // holding up the next cycle.
        xQueueOverwrite(snapshot_mailbox_, &snapshot);
        const AppEvent event = {AppEventType::SnapshotReady};
        xQueueSend(events_, &event, 0);

        const std::uint32_t elapsed_ms = pdTICKS_TO_MS(xTaskGetTickCount() - started);
        if (elapsed_ms > kSampleBudgetMs) {
            ESP_LOGW(TAG, "Sampling cycle took %" PRIu32 " ms of the %" PRIu32 " ms budget", elapsed_ms,
                     kSampleBudgetMs);
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(kSampleIntervalMs));
    }
}

void App::ui_loop()
{
    while (true) {
        AppEvent event;
        if (xQueueReceive(events_, &event, pdMS_TO_TICKS(kUiTickMs)) == pdTRUE) {
            handle_event(event);
        }

        while (hmi_.poll_input(event)) {
            handle_event(event);
        }

        const ConnectivityState state = connectivity_->state();
        if (state != last_connectivity_) {
            last_connectivity_ = state;
            handle_event({AppEventType::ConnectivityChanged});
        }

        if (mode_ == AppMode::Running &&
            pdTICKS_TO_MS(xTaskGetTickCount() - last_interaction_) >= kIdleTimeoutMs) {
            handle_event({AppEventType::IdleTimeout});
        }
    }
}

void App::handle_event(const AppEvent &event)
{
    switch (event.type) {
    case AppEventType::SnapshotReady:
        if (xQueuePeek(snapshot_mailbox_, &snapshot_, 0) == pdTRUE) {
            hmi_.render(snapshot_);
        }
        break;

    case AppEventType::ButtonShort:
        // UI-3: short press toggles the display.
        note_interaction();
        enter_mode(hmi_.display_on() ? AppMode::DisplayOff : AppMode::Running);
        break;

    case AppEventType::ButtonLongReset:
        enter_mode(AppMode::FactoryReset);
        break;

    case AppEventType::Touch:
        // UI-9: a touch wakes the display.
        note_interaction();
        enter_mode(AppMode::Running);
        break;

    case AppEventType::IdleTimeout:
        enter_mode(AppMode::DisplayOff);
        break;

    case AppEventType::ConnectivityChanged: {
        const ConnectivityState state = connectivity_->state();
        hmi_.update_led(state);
        if (state == ConnectivityState::Commissioning) {
            if (mode_ == AppMode::Running) {
                enter_mode(AppMode::Commissioning);
            }
        } else if (mode_ == AppMode::Commissioning) {
            enter_mode(AppMode::Running);
        }
        break;
    }
    }
}

void App::enter_mode(AppMode mode)
{
    if (mode_ == mode) {
        return;
    }
    mode_ = mode;

    switch (mode) {
    case AppMode::Running:
        hmi_.set_display_power(true);
        note_interaction();
        if (snapshot_.sequence > 0) {
            hmi_.render(snapshot_);
        }
        break;

    case AppMode::DisplayOff:
        hmi_.set_display_power(false);
        break;

    case AppMode::Commissioning:
        // TODO: show the commissioning QR code here (UI-6).
        hmi_.set_display_power(true);
        note_interaction();
        break;

    case AppMode::FactoryReset:
        perform_factory_reset();
        break;

    case AppMode::Boot:
        break;
    }
}

void App::perform_factory_reset()
{
    // UI-5. Runs in the app task, never in the button or Matter callback.
    ESP_LOGW(TAG, "Factory reset");
    settings_.factory_reset();
    connectivity_->factory_reset();
    esp_restart();
}

void App::note_interaction()
{
    last_interaction_ = xTaskGetTickCount();
}
