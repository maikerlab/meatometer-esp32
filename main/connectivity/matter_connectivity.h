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
    bool is_commissioned() const override { return commissioned_.load(); }
    bool copy_onboarding(char *qr, std::size_t qr_size, char *manual, std::size_t manual_size) const override;
    void factory_reset() override;

    /** Called from the CHIP event loop. */
    void set_state(ConnectivityState state) { state_.store(state); }
    void note_commissioning_completed() { commissioning_completed_.store(true); }
    void refresh_commissioned();
    /** Leaves commissioning: green only after a completed commission that still has a fabric. */
    void on_window_closed();

private:
    bool load_onboarding();

    std::atomic<ConnectivityState> state_{ConnectivityState::Disconnected};
    std::atomic<bool> commissioned_{false};
    std::atomic<bool> commissioning_completed_{false};
    std::uint16_t endpoint_ids_[kMaxProbes]{};
    std::uint8_t endpoint_count_{0};
    char qr_payload_[kOnboardingPayloadSize]{};
    char manual_code_[kManualPairingCodeSize]{};
    bool onboarding_ready_{false};
};
