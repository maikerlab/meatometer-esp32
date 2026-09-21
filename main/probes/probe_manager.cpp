#include "probes/probe_manager.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_log.h>

static const char *TAG = "probes";

Result ProbeManager::add(TemperatureProbe *probe)
{
    if (probe == nullptr) {
        return Result::InvalidArgument;
    }
    if (probe_count_ >= kMaxProbes) {
        ESP_LOGE(TAG, "Cannot add more than %u probes", kMaxProbes);
        return Result::InvalidArgument;
    }

    probes_[probe_count_++] = probe;
    if (probe->conversion_time_ms() > conversion_time_ms_) {
        conversion_time_ms_ = probe->conversion_time_ms();
    }
    return Result::Ok;
}

Result ProbeManager::init()
{
    Result result = Result::Ok;
    for (std::uint8_t i = 0; i < probe_count_; i++) {
        const Result status = probes_[i]->init();
        if (status != Result::Ok) {
            ESP_LOGE(TAG, "Probe %u failed to initialize", probes_[i]->id());
            result = status;
        }
    }
    return result;
}

void ProbeManager::sample(DeviceSnapshot &out)
{
    for (std::uint8_t i = 0; i < probe_count_; i++) {
        if (probes_[i]->start_conversion() != Result::Ok) {
            ESP_LOGW(TAG, "Probe %u failed to start conversion", probes_[i]->id());
        }
    }

    // One wait for all probes, so conversions overlap instead of adding up.
    if (conversion_time_ms_ > 0) {
        vTaskDelay(pdMS_TO_TICKS(conversion_time_ms_));
    }

    out.sequence = ++sequence_;
    out.probe_count = probe_count_;

    for (std::uint8_t i = 0; i < probe_count_; i++) {
        TemperatureProbe *probe = probes_[i];
        ProbeReading &reading = out.probes[i];

        reading.id = probe->id();
        reading.kind = probe->kind();
        reading.celsius = 0.0f;
        reading.connected = false;
        settings_.name(probe->id(), reading.name, kProbeNameSize);

        if (probe->read(reading.celsius, reading.connected) != Result::Ok) {
            // A bus error is a disconnected probe as far as the UI is
            // concerned (PR-5); the cycle carries on.
            reading.connected = false;
        }
    }
}
