#pragma once

#include <cstdint>

#include "probes/temperature_probe.h"
#include "settings/settings_store.h"
#include "types.h"

/**
 * Owns the probes and produces one DeviceSnapshot per sampling cycle (PR-6).
 *
 * Knows nothing about the display or Matter, which is what keeps local
 * measurement independent of the network (NF-2).
 */
class ProbeManager {
public:
    explicit ProbeManager(const SettingsStore &settings) : settings_(settings) {}

    /** Registers a probe. Not thread-safe; call during startup only. */
    Result add(TemperatureProbe *probe);

    Result init();

    std::uint8_t probe_count() const { return probe_count_; }

    /** Longest conversion the cycle has to wait for, across all probes. */
    std::uint32_t conversion_time_ms() const { return conversion_time_ms_; }

    /**
     * Runs one two-phase cycle: start every conversion, wait once for the
     * slowest, then read everything. Blocks; sampler task only.
     */
    void sample(DeviceSnapshot &out);

private:
    const SettingsStore &settings_;
    TemperatureProbe *probes_[kMaxProbes]{};
    std::uint8_t probe_count_{0};
    std::uint32_t conversion_time_ms_{0};
    std::uint32_t sequence_{0};
};
