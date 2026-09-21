#pragma once

#include <atomic>

#include "connectivity/connectivity.h"

/**
 * Matter end device over Wi-Fi, commissioned via BLE (CN-1, CN-2).
 *
 * One TemperatureMeasurement endpoint per probe (CN-3, CN-4), reported at the
 * sampling rate (CN-5) and clamped to the cluster's range (CN-9).
 */
class MatterConnectivity : public Connectivity {
public:
    Result start(std::uint8_t probe_count) override;
    void publish(const DeviceSnapshot &snapshot) override;
    ConnectivityState state() const override { return state_.load(); }
    void factory_reset() override;

    /** Called from the CHIP event loop. */
    void set_state(ConnectivityState state) { state_.store(state); }

private:
    std::atomic<ConnectivityState> state_{ConnectivityState::Disconnected};
    std::uint16_t endpoint_ids_[kMaxProbes]{};
    std::uint8_t endpoint_count_{0};
};
