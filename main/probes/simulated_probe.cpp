#include "probes/simulated_probe.h"

SimulatedProbe::SimulatedProbe(ProbeId id, ProbeKind kind, const Config &config)
    : TemperatureProbe(id, kind), config_(config)
{
}

Result SimulatedProbe::read(float &celsius, bool &connected)
{
    const std::uint32_t steps = config_.steps_per_ramp > 0 ? config_.steps_per_ramp : 1;
    const std::uint32_t phase = step_ % (2 * steps);
    const std::uint32_t position = phase < steps ? phase : (2 * steps - phase);
    step_++;

    const float span = config_.max_celsius - config_.min_celsius;
    celsius = config_.min_celsius + span * static_cast<float>(position) / static_cast<float>(steps);
    connected = true;
    return Result::Ok;
}
