#pragma once

#include <cstdint>

#include "types.h"

/**
 * Optional network layer. v1 ships NullConnectivity; Matter is v2.
 *
 * publish() is called from the sampler task and must return immediately, so
 * measurement never waits on the network (NF-2).
 */
class Connectivity {
public:
    virtual ~Connectivity() = default;

    /** Brings the stack up and creates one endpoint per probe (CN-3). */
    virtual Result start(std::uint8_t probe_count) = 0;

    virtual void publish(const DeviceSnapshot &snapshot) = 0;

    virtual ConnectivityState state() const = 0;

    /** Erases network and fabric credentials (UI-5). May not return. */
    virtual void factory_reset() = 0;
};
