#include "connectivity/null_connectivity.h"

Result NullConnectivity::start(std::uint8_t probe_count)
{
    (void)probe_count;
    return Result::Ok;
}

void NullConnectivity::publish(const DeviceSnapshot &snapshot)
{
    (void)snapshot;
}
