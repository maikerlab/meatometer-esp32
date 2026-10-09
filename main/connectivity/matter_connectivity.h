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
    bool identify_active() const override;

    /** Called from the CHIP identify callback. */
    void note_identify_start();
    void note_identify_stop();
    void note_identify_effect(std::uint8_t effect_id);
    bool is_commissioned() const override { return commissioned_.load(); }
    void request_pairing() override;
    void cancel_pairing() override;
    bool pairing_active() const override { return pairing_requested_.load(); }
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
    /** CHIP event loop. Closes a window the user did not ask for (UI-6). */
    void close_unsolicited_window();

private:
    /** CHIP event loop. Opens the window for the current rendezvous. */
    void open_pairing_window();
    bool load_onboarding(bool on_network);

    std::atomic<ConnectivityState> state_{ConnectivityState::Disconnected};
    std::atomic<bool> commissioned_{false};
    std::atomic<bool> commissioning_completed_{false};
    /** User pressed Pair. An unsolicited window is closed while this is false. */
    std::atomic<bool> pairing_requested_{false};
    /** Bumped on every Pair press so a close started earlier is ignored. */
    std::atomic<std::uint32_t> pairing_generation_{0};
    /** Generation captured when this object called CloseCommissioningWindow. */
    std::atomic<std::uint32_t> close_epoch_{0};
    /** True after this object asks the stack to close, until that close is delivered. */
    std::atomic<bool> close_pending_{false};
    /** Fabric already present when the current commissioning window opened. */
    std::atomic<bool> retained_fabric_{false};
    std::atomic<std::uint32_t> onboarding_epoch_{0};
    /** Endpoints whose Identify command is still counting down. */
    std::atomic<std::uint8_t> identify_count_{0};
    /** TriggerEffect flash, in FreeRTOS ticks. Ignored unless effect_armed_. */
    std::atomic<std::uint32_t> effect_deadline_{0};
    std::atomic<bool> effect_armed_{false};
    std::uint16_t endpoint_ids_[kMaxProbes]{};
    std::uint8_t endpoint_count_{0};
    mutable std::mutex onboarding_mu_;
    char qr_payload_[kOnboardingPayloadSize]{};
    char manual_code_[kManualPairingCodeSize]{};
    bool onboarding_ready_{false};
};
