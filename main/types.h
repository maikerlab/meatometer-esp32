#pragma once

#include <cstddef>
#include <cstdint>

/** Maximum number of probes on a single I2C hub (NF-4). */
inline constexpr std::uint8_t kMaxProbes = 6;

/** Buffer size for a probe name, including the terminator (PR-7, PR-8). */
inline constexpr std::size_t kProbeNameSize = 32;

/**
 * Outcome of a firmware operation. Shared across HAL, probes, HMI, settings
 * and connectivity so layers can compare and propagate results without
 * converting between per-module enums that would carry the same four codes.
 */
enum class Result {
    Ok,
    Failed,
    InvalidArgument,
    NotReady,
};

/** Status LED colors in UI-4 priority order. */
enum class LedColor {
    Yellow,
    Green,
    Blue,
};

enum class ProbeKind {
    Chamber,
    Food,
};

using ProbeId = std::uint8_t;

struct ProbeReading {
    ProbeId id;
    ProbeKind kind;
    bool connected;
    float celsius;
    char name[kProbeNameSize];
};

enum class ConnectivityState {
    Disabled,
    Disconnected,
    Commissioning,
    Connected,
};

/**
 * One sampling cycle's worth of device state. Values are never clamped here
 * (PR-2, PR-3); clamping happens only when reporting over Matter (CN-9).
 */
struct DeviceSnapshot {
    std::uint32_t sequence;
    std::uint8_t probe_count;
    ProbeReading probes[kMaxProbes];
    bool battery_valid;
    std::uint8_t battery_percent;
    ConnectivityState connectivity;
};
