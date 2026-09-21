#include "probes/max6675_probe.h"

#include <esp_log.h>
#include <max6675.h>

static const char *TAG = "max6675_probe";

/** Datasheet conversion time; the part converts continuously. */
static constexpr std::uint32_t kConversionTimeMs = 220;

/** readCelsius() returns this sentinel when the thermocouple is open (PR-5). */
static constexpr float kOpenCircuitCelsius = -100.0f;

static bool s_initialized = false;

Max6675Probe::Max6675Probe(ProbeId id, const Config &config)
    : TemperatureProbe(id, ProbeKind::Chamber), config_(config)
{
}

std::uint32_t Max6675Probe::conversion_time_ms() const
{
    return kConversionTimeMs;
}

Result Max6675Probe::init()
{
    if (s_initialized) {
        ESP_LOGE(TAG, "Only one MAX6675 instance is supported");
        return Result::InvalidArgument;
    }

    MAX6675_structure cfg = {
        .MAX6675_SCK = config_.sck_gpio,
        .MAX6675_CS = config_.cs_gpio,
        .MAX6675_MISO = config_.miso_gpio,
        .TEMPERATURE_CALIBRATION_COEFFICIENT = config_.calibration_coefficient,
    };
    MAX6675_init(cfg);

    s_initialized = true;
    ready_ = true;
    return Result::Ok;
}

Result Max6675Probe::read(float &celsius, bool &connected)
{
    if (!ready_) {
        connected = false;
        return Result::NotReady;
    }

    // Blocks for roughly 35 ms: the component clocks the bits out with 1 ms
    // vTaskDelay steps. Only the sampler task calls this.
    const float value = static_cast<float>(readCelsius());
    if (value <= kOpenCircuitCelsius) {
        connected = false;
        return Result::Ok;
    }

    celsius = value;
    connected = true;
    return Result::Ok;
}
