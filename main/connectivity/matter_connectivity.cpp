#include "connectivity/matter_connectivity.h"

#include <cstdint>
#include <cstring>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_log.h>

#include <esp_matter.h>
#include <esp_matter_console.h>
#include <esp_matter_ota.h>

#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>
#include <clusters/temperature_measurement/integration.h>
#include <platform/ConnectivityManager.h>
#include <platform/PlatformManager.h>
#include <setup_payload/OnboardingCodesUtil.h>
#include <setup_payload/QRCodeSetupPayloadGenerator.h>

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

/** Commissioning window opened from the Pair button (UI-6). */
static constexpr auto k_timeout_seconds = 300;

/** CN-9: the cluster cannot represent more than 327.67 C. */
static constexpr std::int16_t kMeasuredValueMax = 32767;
static constexpr std::int16_t kMeasuredValueMin = -27315;

static_assert(kOnboardingPayloadSize >=
              chip::QRCodeBasicSetupPayloadGenerator::kMaxQRCodeBase38RepresentationLength + 1);
static_assert(kManualPairingCodeSize >= static_cast<std::size_t>(chip::kManualSetupLongCodeCharLength + 2));

static MatterConnectivity *s_instance = nullptr;

static void set_state(ConnectivityState state);

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
        // An address arrives mid-pairing, before the commission succeeds or
        // fails. Leave Commissioning alone so the QR stays up and the LED
        // stays blue until the window closes.
        if (s_instance == nullptr || s_instance->state() == ConnectivityState::Commissioning ||
            !s_instance->is_commissioned()) {
            break;
        }
        set_state(ConnectivityState::Connected);
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning complete");
        if (s_instance != nullptr) {
            s_instance->note_commissioning_completed();
            s_instance->refresh_commissioned();
        }
        // With no fabric left, that complete is the removal finishing, not a
        // successful join. Pair opens the next window (UI-6).
        if (chip::Server::GetInstance().GetFabricTable().FabricCount() == 0) {
            set_state(ConnectivityState::Disconnected);
        } else {
            set_state(ConnectivityState::Connected);
        }
        MEMORY_PROFILER_DUMP_HEAP_STAT("commissioning complete");
        break;

    case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
        ESP_LOGI(TAG, "Commissioning failed, fail safe timer expired");
        // A failed commission does not reopen the window. Pair does that (UI-6).
        if (chip::Server::GetInstance().GetFabricTable().FabricCount() == 0) {
            set_state(ConnectivityState::Disconnected);
        }
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStarted:
        ESP_LOGI(TAG, "Commissioning session started");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStopped:
        ESP_LOGI(TAG, "Commissioning session stopped");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowOpened:
        ESP_LOGI(TAG, "Commissioning window opened");
        if (s_instance == nullptr || !s_instance->pairing_active()) {
            // Boot, and removing the last fabric, open a window on their own.
            // Close it on the next CHIP turn so this callback can finish.
            chip::DeviceLayer::SystemLayer().ScheduleLambda([]() {
                if (s_instance != nullptr) {
                    s_instance->close_unsolicited_window();
                }
            });
            break;
        }
        s_instance->note_window_opened();
        set_state(ConnectivityState::Commissioning);
        MEMORY_PROFILER_DUMP_HEAP_STAT("commissioning window opened");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowClosed:
        ESP_LOGI(TAG, "Commissioning window closed");
        if (s_instance != nullptr) {
            s_instance->on_window_closed();
            s_instance->refresh_commissioned();
        }
        break;

    case chip::DeviceLayer::DeviceEventType::kFabricRemoved: {
        ESP_LOGI(TAG, "Fabric removed successfully");
        if (s_instance != nullptr) {
            s_instance->refresh_commissioned();
        }
        // Wi-Fi credentials may still be present. Pair opens the matching
        // window later; this event does not (UI-6).
        set_state(ConnectivityState::Disconnected);
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
        if (s_instance != nullptr) {
            s_instance->refresh_commissioned();
        }
        break;

    case chip::DeviceLayer::DeviceEventType::kBLEDeinitialized:
        ESP_LOGI(TAG, "BLE deinitialized and memory reclaimed");
        MEMORY_PROFILER_DUMP_HEAP_STAT("BLE deinitialized");
        break;

    default:
        break;
    }
}

/** TriggerEffect has no STOP, so Blink/Breathe/Okay/ChannelChange flash this long. */
static constexpr std::uint32_t kEffectFlashMs = 15000;

static constexpr std::uint8_t kFinishEffect = 0xFE;
static constexpr std::uint8_t kStopEffect = 0xFF;

static esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
                                       uint8_t effect_variant, void *priv_data)
{
    ESP_LOGI(TAG, "Identification callback: type: %u, endpoint: %u, effect: %u, variant: %u", type, endpoint_id,
             effect_id, effect_variant);
    if (s_instance == nullptr) {
        return ESP_OK;
    }

    switch (type) {
    case identification::START:
        s_instance->note_identify_start();
        break;
    case identification::STOP:
        s_instance->note_identify_stop();
        break;
    case identification::EFFECT:
        s_instance->note_identify_effect(effect_id);
        break;
    }
    (void)priv_data;
    return ESP_OK;
}

bool MatterConnectivity::identify_active() const
{
    if (identify_count_.load() > 0) {
        return true;
    }
    if (!effect_armed_.load()) {
        return false;
    }

    const auto now = static_cast<std::uint32_t>(xTaskGetTickCount());
    return static_cast<std::int32_t>(now - effect_deadline_.load()) < 0;
}

void MatterConnectivity::note_identify_start()
{
    std::uint8_t current = identify_count_.load();
    do {
        if (current == UINT8_MAX) {
            return;
        }
    } while (!identify_count_.compare_exchange_weak(current, static_cast<std::uint8_t>(current + 1)));
}

void MatterConnectivity::note_identify_stop()
{
    effect_armed_.store(false);
    std::uint8_t current = identify_count_.load();
    do {
        if (current == 0) {
            return;
        }
    } while (!identify_count_.compare_exchange_weak(current, static_cast<std::uint8_t>(current - 1)));
}

void MatterConnectivity::note_identify_effect(std::uint8_t effect_id)
{
    if (effect_id == kFinishEffect || effect_id == kStopEffect) {
        return;
    }

    effect_deadline_.store(static_cast<std::uint32_t>(xTaskGetTickCount()) + pdMS_TO_TICKS(kEffectFlashMs));
    effect_armed_.store(true);
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

    chip::DeviceLayer::PlatformMgr().LockChipStack();
    refresh_commissioned();
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();

    if (!load_onboarding(false)) {
        ESP_LOGW(TAG, "Failed to read the Matter onboarding payload");
    }

    return Result::Ok;
}

void MatterConnectivity::refresh_commissioned()
{
    commissioned_.store(chip::Server::GetInstance().GetFabricTable().FabricCount() > 0);
}

void MatterConnectivity::note_window_opened()
{
    refresh_commissioned();
    retained_fabric_.store(commissioned_.load());
}

void MatterConnectivity::request_pairing()
{
    pairing_generation_.fetch_add(1);
    pairing_requested_.store(true);
    chip::DeviceLayer::SystemLayer().ScheduleLambda([]() {
        if (s_instance != nullptr) {
            s_instance->open_pairing_window();
        }
    });
}

void MatterConnectivity::cancel_pairing()
{
    pairing_requested_.store(false);
    chip::DeviceLayer::SystemLayer().ScheduleLambda([]() {
        if (s_instance != nullptr) {
            s_instance->close_unsolicited_window();
        }
    });
}

void MatterConnectivity::close_unsolicited_window()
{
    if (pairing_requested_.load()) {
        return;
    }
    chip::CommissioningWindowManager &commissionMgr = chip::Server::GetInstance().GetCommissioningWindowManager();
    if (!commissionMgr.IsCommissioningWindowOpen()) {
        return;
    }
    close_epoch_.store(pairing_generation_.load());
    close_pending_.store(true);
    commissionMgr.CloseCommissioningWindow();
}

void MatterConnectivity::open_pairing_window()
{
    if (!pairing_requested_.load()) {
        return;
    }
    if (chip::Server::GetInstance().GetFabricTable().FabricCount() != 0) {
        pairing_requested_.store(false);
        return;
    }

    const bool on_network = chip::DeviceLayer::ConnectivityMgr().IsWiFiStationProvisioned();
    load_onboarding(on_network);

    chip::CommissioningWindowManager &commissionMgr = chip::Server::GetInstance().GetCommissioningWindowManager();
    if (commissionMgr.IsCommissioningWindowOpen()) {
        set_state(ConnectivityState::Commissioning);
        return;
    }

    constexpr auto kTimeoutSeconds = chip::System::Clock::Seconds32(k_timeout_seconds);
    const auto advertisement = on_network ? chip::CommissioningWindowAdvertisement::kDnssdOnly
                                          : chip::CommissioningWindowAdvertisement::kAllSupported;
    CHIP_ERROR err = commissionMgr.OpenBasicCommissioningWindow(kTimeoutSeconds, advertisement);
    if (err != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "Failed to open commissioning window, err:%" CHIP_ERROR_FORMAT, err.Format());
        pairing_requested_.store(false);
        return;
    }
    set_state(ConnectivityState::Commissioning);
}

void MatterConnectivity::on_window_closed()
{
    if (close_pending_.exchange(false)) {
        // A close this object started. Ignore it when Pair was pressed again
        // after that close was issued, so the new request is not dropped.
        if (pairing_generation_.load() != close_epoch_.load()) {
            return;
        }
    }
    pairing_requested_.store(false);
    // Do not require Commissioning here. Wi-Fi stores Connected as soon as it
    // has an address, which is the usual path through pairing, so the window
    // often closes from Connected. A fail-safe or timeout with no fabric must
    // still leave the LED yellow.
    const bool has_fabric = chip::Server::GetInstance().GetFabricTable().FabricCount() > 0;
    const bool paired = commissioning_completed_.load() || retained_fabric_.load();
    if (has_fabric && paired) {
        set_state(ConnectivityState::Connected);
    } else {
        set_state(ConnectivityState::Disconnected);
    }
}

bool MatterConnectivity::load_on_network_onboarding()
{
    return load_onboarding(true);
}

bool MatterConnectivity::load_onboarding(bool on_network)
{
    const chip::RendezvousInformationFlags flags(on_network ? chip::RendezvousInformationFlag::kOnNetwork
                                                            : chip::RendezvousInformationFlag::kBLE);

    char qr[kOnboardingPayloadSize];
    char manual[kManualPairingCodeSize];
    chip::MutableCharSpan qr_span(qr, sizeof(qr));
    CHIP_ERROR err = GetQRCode(qr_span, flags);
    if (err != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "GetQRCode failed: %" CHIP_ERROR_FORMAT, err.Format());
        return false;
    }

    chip::MutableCharSpan manual_span(manual, sizeof(manual));
    err = GetManualPairingCode(manual_span, flags);
    if (err != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "GetManualPairingCode failed: %" CHIP_ERROR_FORMAT, err.Format());
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(onboarding_mu_);
        std::strncpy(qr_payload_, qr, sizeof(qr_payload_) - 1);
        qr_payload_[sizeof(qr_payload_) - 1] = '\0';
        std::strncpy(manual_code_, manual, sizeof(manual_code_) - 1);
        manual_code_[sizeof(manual_code_) - 1] = '\0';
        onboarding_ready_ = true;
        onboarding_epoch_.fetch_add(1);
    }

    ESP_LOGI(TAG, "Onboarding payload ready (%s)", on_network ? "on-network" : "BLE");
    return true;
}

bool MatterConnectivity::copy_onboarding(char *qr, std::size_t qr_size, char *manual, std::size_t manual_size) const
{
    if (qr == nullptr || manual == nullptr || qr_size == 0 || manual_size == 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(onboarding_mu_);
    if (!onboarding_ready_) {
        return false;
    }

    std::strncpy(qr, qr_payload_, qr_size - 1);
    qr[qr_size - 1] = '\0';
    std::strncpy(manual, manual_code_, manual_size - 1);
    manual[manual_size - 1] = '\0';
    return true;
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
