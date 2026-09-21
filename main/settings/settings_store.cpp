#include "settings/settings_store.h"

#include <cstdio>
#include <cstring>

#include <esp_log.h>
#include <nvs.h>
#include <nvs_flash.h>

#include "esp_status.h"

static const char *TAG = "settings";

static const char *kNamespace = "meatometer";

/** Default probe names (PR-7). Ports beyond the first three get "Probe N". */
static const char *kDefaultNames[] = {"Environment", "Meat 1", "Meat 2"};

static void name_key(ProbeId id, char *out, std::size_t size)
{
    snprintf(out, size, "name%u", static_cast<unsigned>(id));
}

void SettingsStore::default_name(ProbeId id, char *out, std::size_t size)
{
    if (id < sizeof(kDefaultNames) / sizeof(kDefaultNames[0])) {
        snprintf(out, size, "%s", kDefaultNames[id]);
    } else {
        snprintf(out, size, "Probe %u", static_cast<unsigned>(id) + 1);
    }
}

void SettingsStore::load_defaults()
{
    for (ProbeId id = 0; id < kMaxProbes; id++) {
        default_name(id, names_[id], kProbeNameSize);
    }
}

Result SettingsStore::init()
{
    mutex_ = xSemaphoreCreateMutexStatic(&mutex_storage_);
    if (mutex_ == nullptr) {
        return Result::Failed;
    }

    load_defaults();

    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        // Nothing stored yet; defaults stand.
        return Result::Ok;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return from_esp_err(err);
    }

    for (ProbeId id = 0; id < kMaxProbes; id++) {
        char key[16];
        name_key(id, key, sizeof(key));
        std::size_t length = kProbeNameSize;
        if (nvs_get_str(handle, key, names_[id], &length) != ESP_OK) {
            default_name(id, names_[id], kProbeNameSize);
        }
    }
    nvs_close(handle);

    return Result::Ok;
}

void SettingsStore::name(ProbeId id, char *out, std::size_t size) const
{
    if (id >= kMaxProbes || out == nullptr || size == 0) {
        return;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    snprintf(out, size, "%s", names_[id]);
    xSemaphoreGive(mutex_);
}

Result SettingsStore::set_name(ProbeId id, const char *name)
{
    if (id >= kMaxProbes || name == nullptr || name[0] == '\0') {
        return Result::InvalidArgument;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return from_esp_err(err);
    }

    char key[16];
    name_key(id, key, sizeof(key));
    err = nvs_set_str(handle, key, name);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to store name for probe %u: %s", id, esp_err_to_name(err));
        return from_esp_err(err);
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    snprintf(names_[id], kProbeNameSize, "%s", name);
    xSemaphoreGive(mutex_);

    return Result::Ok;
}

Result SettingsStore::factory_reset()
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_erase_all(handle);
        if (err == ESP_OK) {
            err = nvs_commit(handle);
        }
        nvs_close(handle);
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    load_defaults();
    xSemaphoreGive(mutex_);

    return from_esp_err(err);
}
