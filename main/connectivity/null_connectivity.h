#pragma once

#include "connectivity/connectivity.h"

/** v1: no network at all. The status LED stays yellow (UI-4). */
class NullConnectivity : public Connectivity {
public:
    Result start(std::uint8_t probe_count) override;
    void publish(const DeviceSnapshot &snapshot) override;
    ConnectivityState state() const override { return ConnectivityState::Disabled; }
    void factory_reset() override {}
};
