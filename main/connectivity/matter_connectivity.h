#pragma once

#include <atomic>
#include <mutex>

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
    std::uint32_t onboarding_epoch() const override { return onboarding_epoch_.load(); }
    void factory_reset() override;

    /** Called from the CHIP event loop. */
    void set_state(ConnectivityState state) { state_.store(state); }
    /**
     * On-network QR for the DNS-SD-only window opened after the last fabric
     * leaves. BLE is not advertising in that window.
     */
    bool load_on_network_onboarding();
    void note_commissioning_completed() { commissioning_completed_.store(true); }
    void note_window_opened();
    void refresh_commissioned();
    /**
     * Leaves commissioning. Green only when a fabric is still present and it
     * was already there when this window opened, or this commission completed.
     * Wi-Fi having an address is not enough: that happens before pairing finishes.
     */
    void on_window_closed();

private:
    bool load_onboarding(bool on_network);

    std::atomic<ConnectivityState> state_{ConnectivityState::Disconnected};
    std::atomic<bool> commissioned_{false};
    std::atomic<bool> commissioning_completed_{false};
    /** Fabric already present when the current commissioning window opened. */
    std::atomic<bool> retained_fabric_{false};
    std::atomic<std::uint32_t> onboarding_epoch_{0};
    std::uint16_t endpoint_ids_[kMaxProbes]{};
    std::uint8_t endpoint_count_{0};
    mutable std::mutex onboarding_mu_;
    char qr_payload_[kOnboardingPayloadSize]{};
    char manual_code_[kManualPairingCodeSize]{};
    bool onboarding_ready_{false};
};
