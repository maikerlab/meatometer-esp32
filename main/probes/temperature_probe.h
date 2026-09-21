#pragma once

#include <cstdint>

#include "types.h"

/**
 * One temperature probe port.
 *
 * Reading is two-phase so that probes with a conversion delay can convert in
 * parallel: ProbeManager calls start_conversion() on every probe, waits once
 * for the longest conversion_time_ms(), then calls read() on every probe.
 */
class TemperatureProbe {
public:
    TemperatureProbe(ProbeId id, ProbeKind kind) : id_(id), kind_(kind) {}
    virtual ~TemperatureProbe() = default;

    ProbeId id() const { return id_; }
    ProbeKind kind() const { return kind_; }

    /** Time the hardware needs between start_conversion() and read(). */
    virtual std::uint32_t conversion_time_ms() const { return 0; }

    virtual Result init() { return Result::Ok; }

    virtual Result start_conversion() { return Result::Ok; }

    /**
     * Latest value. `connected` reports probe presence (PR-5); `celsius` is
     * only meaningful when connected and is never clamped (PR-2, PR-3).
     */
    virtual Result read(float &celsius, bool &connected) = 0;

private:
    ProbeId id_;
    ProbeKind kind_;
};
