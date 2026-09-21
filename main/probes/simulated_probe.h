#pragma once

#include "probes/temperature_probe.h"

/**
 * Deterministic stand-in for probe hardware that is not wired yet, so the
 * sampling loop, HMI and Matter path can be exercised without a grill.
 *
 * Ramps linearly between two temperatures and back, one step per read.
 */
class SimulatedProbe : public TemperatureProbe {
public:
    struct Config {
        float min_celsius;
        float max_celsius;
        std::uint32_t steps_per_ramp;
    };

    SimulatedProbe(ProbeId id, ProbeKind kind, const Config &config);

    Result read(float &celsius, bool &connected) override;

private:
    Config config_;
    std::uint32_t step_{0};
};
