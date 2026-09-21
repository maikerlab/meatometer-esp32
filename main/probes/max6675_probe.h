#pragma once

#include "probes/temperature_probe.h"

/**
 * Type-K chamber probe on a MAX6675 breakout (bit-banged SPI).
 *
 * The `hayschan/max6675` component keeps its pin configuration in file-scope
 * state, so only one instance may exist. init() rejects a second one. That
 * restriction goes away with KmeterProbe, which is the intended replacement
 * once the M5Stack Kmeter Unit is in hand (ADR 01).
 */
class Max6675Probe : public TemperatureProbe {
public:
    struct Config {
        int sck_gpio;
        int cs_gpio;
        int miso_gpio;
        float calibration_coefficient;
    };

    Max6675Probe(ProbeId id, const Config &config);

    std::uint32_t conversion_time_ms() const override;
    Result init() override;
    Result read(float &celsius, bool &connected) override;

private:
    Config config_;
    bool ready_{false};
};
