#pragma once

#include <cstddef>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "types.h"

/**
 * Persisted user settings (PR-7, PR-8, UI-5).
 *
 * Names are cached in RAM so the sampler task never touches NVS; writes come
 * from the app task only.
 */
class SettingsStore {
public:
    Result init();

    /** Copies the current name for `id` into `out`. Safe from any task. */
    void name(ProbeId id, char *out, std::size_t size) const;

    /** Renames a probe and persists it (PR-8). App task only. */
    Result set_name(ProbeId id, const char *name);

    /** Restores every persisted setting to its default (UI-5). */
    Result factory_reset();

    static void default_name(ProbeId id, char *out, std::size_t size);

private:
    void load_defaults();

    SemaphoreHandle_t mutex_{nullptr};
    StaticSemaphore_t mutex_storage_{};
    char names_[kMaxProbes][kProbeNameSize]{};
};
