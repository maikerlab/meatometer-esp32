#include "connectivity/matter_connectivity.h"

#include <esp_log.h>

#include <esp_matter.h>
#include <esp_matter_console.h>
#include <esp_matter_ota.h>

#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>
#include <clusters/temperature_measurement/integration.h>

#include <log_heap_numbers.h>

#include "esp_status.h"

#ifdef CONFIG_ENABLE_SET_CERT_DECLARATION_API
#include <esp_matter_providers.h>
#include <lib/support/Span.h>
#ifdef CONFIG_SEC_CERT_DAC_PROVIDER
#include <platform/ESP32/ESP32SecureCertDACProvider.h>
#elif defined(CONFIG_FACTORY_PARTITION_DAC_PROVIDER)
#include <platform/ESP32/ESP32FactoryDataProvider.h>
#endif
using namespace chip::DeviceLayer;
#endif

using namespace esp_matter;
using namespace esp_matter::endpoint;

static const char *TAG = "matter";

/** Commissioning window kept open for this long after the last fabric leaves. */
static constexpr auto k_timeout_seconds = 300;

/** CN-9: the cluster cannot represent more than 327.67 C. */
static constexpr std::int16_t kMeasuredValueMax = 32767;
static constexpr std::int16_t kMeasuredValueMin = -27315;

static MatterConnectivity *s_instance = nullptr;

#ifdef CONFIG_ENABLE_SET_CERT_DECLARATION_API
extern const uint8_t cd_start[] asm("_binary_certification_declaration_der_start");
extern const uint8_t cd_end[] asm("_binary_certification_declaration_der_end");

const chip::ByteSpan cdSpan(cd_start, static_cast<size_t>(cd_end - cd_start));
#endif

#if CONFIG_ENABLE_ENCRYPTED_OTA
extern const char decryption_key_start[] asm("_binary_esp_image_encryption_key_pem_start");
extern const char decryption_key_end[] asm("_binary_esp_image_encryption_key_pem_end");

static const char *s_decryption_key = decryption_key_start;
static const uint16_t s_decryption_key_len = decryption_key_end - decryption_key_start;
#endif

static void set_state(ConnectivityState state)
{
    if (s_instance != nullptr) {
        s_instance->set_state(state);
    }
}

static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kInterfaceIpAddressChanged:
        ESP_LOGI(TAG, "Interface IP Address changed");
        set_state(ConnectivityState::Connected);
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning complete");
        set_state(ConnectivityState::Connected);
        MEMORY_PROFILER_DUMP_HEAP_STAT("commissioning complete");
        break;

    case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
        ESP_LOGI(TAG, "Commissioning failed, fail safe timer expired");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStarted:
        ESP_LOGI(TAG, "Commissioning session started");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStopped:
        ESP_LOGI(TAG, "Commissioning session stopped");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowOpened:
        ESP_LOGI(TAG, "Commissioning window opened");
        set_state(ConnectivityState::Commissioning);
        MEMORY_PROFILER_DUMP_HEAP_STAT("commissioning window opened");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowClosed:
        ESP_LOGI(TAG, "Commissioning window closed");
        break;

    case chip::DeviceLayer::DeviceEventType::kFabricRemoved: {
        ESP_LOGI(TAG, "Fabric removed successfully");
        set_state(ConnectivityState::Disconnected);
        if (chip::Server::GetInstance().GetFabricTable().FabricCount() == 0) {
            chip::CommissioningWindowManager &commissionMgr =
                chip::Server::GetInstance().GetCommissioningWindowManager();
            constexpr auto kTimeoutSeconds = chip::System::Clock::Seconds16(k_timeout_seconds);
            if (!commissionMgr.IsCommissioningWindowOpen()) {
                /* Wi-Fi credentials are kept, so only advertise on DNS-SD. */
                CHIP_ERROR err = commissionMgr.OpenBasicCommissioningWindow(
                    kTimeoutSeconds, chip::CommissioningWindowAdvertisement::kDnssdOnly);
                if (err != CHIP_NO_ERROR) {
                    ESP_LOGE(TAG, "Failed to open commissioning window, err:%" CHIP_ERROR_FORMAT, err.Format());
                }
            }
        }
        break;
    }

    case chip::DeviceLayer::DeviceEventType::kFabricWillBeRemoved:
        ESP_LOGI(TAG, "Fabric will be removed");
        break;

    case chip::DeviceLayer::DeviceEventType::kFabricUpdated:
        ESP_LOGI(TAG, "Fabric is updated");
        break;

    case chip::DeviceLayer::DeviceEventType::kFabricCommitted:
        ESP_LOGI(TAG, "Fabric is committed");
        break;

    case chip::DeviceLayer::DeviceEventType::kBLEDeinitialized:
        ESP_LOGI(TAG, "BLE deinitialized and memory reclaimed");
        MEMORY_PROFILER_DUMP_HEAP_STAT("BLE deinitialized");
        break;

    default:
        break;
    }
}

static esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
                                       uint8_t effect_variant, void *priv_data)
{
    ESP_LOGI(TAG, "Identification callback: type: %u, effect: %u, variant: %u", type, effect_id, effect_variant);
    return ESP_OK;
}

static esp_err_t app_attribute_update_cb(attribute::callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
                                         uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data)
{
    // Nothing on this device is driven by a writable attribute yet; probe
    // renaming over Matter is CN-8 (v2).
    return ESP_OK;
}

Result MatterConnectivity::start(std::uint8_t probe_count)
{
    s_instance = this;

    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    if (node == nullptr) {
        ESP_LOGE(TAG, "Failed to create Matter node");
        return Result::Failed;
    }

    MEMORY_PROFILER_DUMP_HEAP_STAT("node created");

    // One endpoint per probe (CN-3). MinMeasuredValue/MaxMeasuredValue are
    // left at their defaults and not reported (CN-4).
    for (std::uint8_t i = 0; i < probe_count && i < kMaxProbes; i++) {
        temperature_sensor::config_t config;
        endpoint_t *endpoint = temperature_sensor::create(node, &config, ENDPOINT_FLAG_NONE, nullptr);
        if (endpoint == nullptr) {
            ESP_LOGE(TAG, "Failed to create temperature endpoint for probe %u", i);
            return Result::Failed;
        }
        endpoint_ids_[i] = endpoint::get_id(endpoint);
        endpoint_count_++;
        ESP_LOGI(TAG, "Probe %u reports on endpoint %u", i, endpoint_ids_[i]);
    }

#ifdef CONFIG_ENABLE_SET_CERT_DECLARATION_API
    auto *dac_provider = get_dac_provider();
#ifdef CONFIG_SEC_CERT_DAC_PROVIDER
    static_cast<ESP32SecureCertDACProvider *>(dac_provider)->SetCertificationDeclaration(cdSpan);
#elif defined(CONFIG_FACTORY_PARTITION_DAC_PROVIDER)
    static_cast<ESP32FactoryDataProvider *>(dac_provider)->SetCertificationDeclaration(cdSpan);
#endif
#endif

    esp_err_t err = esp_matter::start(app_event_cb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Matter, err:%d", err);
        return from_esp_err(err);
    }

    MEMORY_PROFILER_DUMP_HEAP_STAT("matter started");

#if CONFIG_ENABLE_ENCRYPTED_OTA
    err = esp_matter_ota_requestor_encrypted_init(s_decryption_key, s_decryption_key_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize encrypted OTA, err: %d", err);
        return from_esp_err(err);
    }
#endif

#if CONFIG_ENABLE_CHIP_SHELL
    esp_matter::console::diagnostics_register_commands();
    esp_matter::console::wifi_register_commands();
    esp_matter::console::factoryreset_register_commands();
    esp_matter::console::attribute_register_commands();
    esp_matter::console::init();
#endif

    return Result::Ok;
}

void MatterConnectivity::publish(const DeviceSnapshot &snapshot)
{
    for (std::uint8_t i = 0; i < snapshot.probe_count && i < endpoint_count_; i++) {
        const ProbeReading &reading = snapshot.probes[i];
        if (!reading.connected) {
            continue;
        }

        // CN-9: the display shows the real value, Matter gets it clamped.
        float hundredths = reading.celsius * 100.0f;
        if (hundredths > static_cast<float>(kMeasuredValueMax)) {
            hundredths = static_cast<float>(kMeasuredValueMax);
        } else if (hundredths < static_cast<float>(kMeasuredValueMin)) {
            hundredths = static_cast<float>(kMeasuredValueMin);
        }

        const uint16_t endpoint_id = endpoint_ids_[i];
        const int16_t measured_value = static_cast<int16_t>(hundredths);

        // Hands the write to the CHIP event loop and returns immediately, so
        // the sampler never blocks on the stack.
        chip::DeviceLayer::SystemLayer().ScheduleLambda([endpoint_id, measured_value]() {
            CHIP_ERROR err = chip::app::Clusters::TemperatureMeasurement::SetMeasuredValue(endpoint_id, measured_value);
            if (err != CHIP_NO_ERROR) {
                ESP_LOGE(TAG, "SetMeasuredValue failed: %" CHIP_ERROR_FORMAT, err.Format());
            }
        });
    }
}

void MatterConnectivity::factory_reset()
{
    // Erases fabrics and Wi-Fi credentials, then reboots (UI-5).
    esp_matter::factory_reset();
}
