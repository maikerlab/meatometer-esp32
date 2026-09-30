#pragma once

#include <cstddef>
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

    /**
     * False only for a Matter node that has no fabric yet. Null connectivity
     * is not a Matter device, so it reports commissioned and the probe screen
     * stays up.
     */
    virtual bool is_commissioned() const { return true; }

    /**
     * Copies the onboarding QR payload and the manual pairing code for the
     * rendezvous method of the current window. Returns false when there is
     * nothing to pair with.
     */
    virtual bool copy_onboarding(char *qr, std::size_t qr_size, char *manual, std::size_t manual_size) const
    {
        (void)qr;
        (void)qr_size;
        (void)manual;
        (void)manual_size;
        return false;
    }

    /** Bumps each time the onboarding payload is regenerated. */
    virtual std::uint32_t onboarding_epoch() const { return 0; }

    /** Erases network and fabric credentials (UI-5). May not return. */
    virtual void factory_reset() = 0;
};

/** `MT:` payload, including the terminator. Matches the CHIP QR buffer. */
inline constexpr std::size_t kOnboardingPayloadSize = 129;

/** Long manual code, check digit and terminator. */
inline constexpr std::size_t kManualPairingCodeSize = 22;
