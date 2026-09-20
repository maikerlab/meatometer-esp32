/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_err.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <nvs_flash.h>

#include <esp_matter.h>
#include <esp_matter_console.h>
#include <esp_matter_ota.h>

#include <common_macros.h>
#include <log_heap_numbers.h>

#include <app_priv.h>
#include <app_reset.h>
#include <hal_esp_idf.h>

#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>

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

#include <clusters/temperature_measurement/integration.h>

static const char *TAG = "app_main";
uint16_t light_endpoint_id = 0;
uint16_t temperature_endpoint_id = 0;
static EspIdfHal s_hal;

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

constexpr auto k_timeout_seconds = 300;

#ifdef CONFIG_ENABLE_SET_CERT_DECLARATION_API
extern const uint8_t
    cd_start[] asm("_binary_certification_declaration_der_start");
extern const uint8_t cd_end[] asm("_binary_certification_declaration_der_end");

const chip::ByteSpan cdSpan(cd_start, static_cast<size_t>(cd_end - cd_start));
#endif // CONFIG_ENABLE_SET_CERT_DECLARATION_API

#if CONFIG_ENABLE_ENCRYPTED_OTA
extern const char
    decryption_key_start[] asm("_binary_esp_image_encryption_key_pem_start");
extern const char
    decryption_key_end[] asm("_binary_esp_image_encryption_key_pem_end");

static const char *s_decryption_key = decryption_key_start;
static const uint16_t s_decryption_key_len =
    decryption_key_end - decryption_key_start;
#endif // CONFIG_ENABLE_ENCRYPTED_OTA

static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg) {
  switch (event->Type) {
  case chip::DeviceLayer::DeviceEventType::kInterfaceIpAddressChanged:
    ESP_LOGI(TAG, "Interface IP Address changed");
    break;

  case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
    ESP_LOGI(TAG, "Commissioning complete");
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
    MEMORY_PROFILER_DUMP_HEAP_STAT("commissioning window opened");
    break;

  case chip::DeviceLayer::DeviceEventType::kCommissioningWindowClosed:
    ESP_LOGI(TAG, "Commissioning window closed");
    break;

  case chip::DeviceLayer::DeviceEventType::kFabricRemoved: {
    ESP_LOGI(TAG, "Fabric removed successfully");
    if (chip::Server::GetInstance().GetFabricTable().FabricCount() == 0) {
      chip::CommissioningWindowManager &commissionMgr =
          chip::Server::GetInstance().GetCommissioningWindowManager();
      constexpr auto kTimeoutSeconds =
          chip::System::Clock::Seconds16(k_timeout_seconds);
      if (!commissionMgr.IsCommissioningWindowOpen()) {
        /* After removing last fabric, this example does not remove the Wi-Fi
         * credentials and still has IP connectivity so, only advertising on
         * DNS-SD.
         */
        CHIP_ERROR err = commissionMgr.OpenBasicCommissioningWindow(
            kTimeoutSeconds,
            chip::CommissioningWindowAdvertisement::kDnssdOnly);
        if (err != CHIP_NO_ERROR) {
          ESP_LOGE(
              TAG,
              "Failed to open commissioning window, err:%" CHIP_ERROR_FORMAT,
              err.Format());
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

// This callback is invoked when clients interact with the Identify Cluster.
// In the callback implementation, an endpoint can identify itself. (e.g., by
// flashing an LED or light).
static esp_err_t app_identification_cb(identification::callback_type_t type,
                                       uint16_t endpoint_id, uint8_t effect_id,
                                       uint8_t effect_variant,
                                       void *priv_data) {
  ESP_LOGI(TAG, "Identification callback: type: %u, effect: %u, variant: %u",
           type, effect_id, effect_variant);
  return ESP_OK;
}

// This callback is called for every attribute update. The callback
// implementation shall handle the desired attributes and return an appropriate
// error code. If the attribute is not of your interest, please do not return an
// error code and strictly return ESP_OK.
static esp_err_t
app_attribute_update_cb(attribute::callback_type_t type, uint16_t endpoint_id,
                        uint32_t cluster_id, uint32_t attribute_id,
                        esp_matter_attr_val_t *val, void *priv_data) {
  esp_err_t err = ESP_OK;

  if (type == PRE_UPDATE) {
    /* Driver update */
    app_driver_handle_t driver_handle = (app_driver_handle_t)priv_data;
    err = app_driver_attribute_update(driver_handle, endpoint_id, cluster_id,
                                      attribute_id, val);
  }

  return err;
}

void MeasureTemperature_Task(void *pvParameters) {
  Hal *hal = static_cast<Hal *>(pvParameters);
  while (1) {
    double sum = 0;
    int count = 0;
    for (int i = 0; i < 5; i++) {
      float data = 0.0f;
      if (hal == nullptr ||
          hal->read_temperature(data) != HalStatus::Ok) {
        ESP_LOGE(TAG, "Failed to read temperature");
        vTaskDelay(100 / portTICK_PERIOD_MS);
        continue;
      }
      sum += data;
      count++;
      vTaskDelay(100 /
                 portTICK_PERIOD_MS); // add a short delay between readings
    }
    if (count == 0) {
      vTaskDelay(5000 / portTICK_PERIOD_MS);
      continue;
    }
    double mean = sum / count;
    ESP_LOGI(TAG, "Mean temperature: %.0f °C", mean);

    const uint16_t endpoint_id = temperature_endpoint_id;
    const int16_t measured_value = static_cast<int16_t>(mean * 100);
    chip::DeviceLayer::SystemLayer().ScheduleLambda([endpoint_id,
                                                     measured_value]() {
      CHIP_ERROR err =
          TemperatureMeasurement::SetMeasuredValue(endpoint_id, measured_value);
      if (err != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "SetMeasuredValue failed: %" CHIP_ERROR_FORMAT,
                 err.Format());
      }
    });

    vTaskDelay(5000 / portTICK_PERIOD_MS);
  }
}

extern "C" void app_main() {
  esp_err_t err = ESP_OK;

  /* Initialize the ESP NVS layer */
  nvs_flash_init();

  MEMORY_PROFILER_DUMP_HEAP_STAT("Bootup");

  ABORT_APP_ON_FAILURE(s_hal.init() == HalStatus::Ok,
                       ESP_LOGE(TAG, "Failed to initialize HAL"));
  s_hal.set_led(LedColor::Yellow);

  /* Initialize driver */
  app_driver_handle_t button_handle = app_driver_button_init();
  app_reset_button_register(button_handle);

  /* Create a Matter node and add the mandatory Root Node device type on
   * endpoint 0 */
  node::config_t node_config;

  // node handle can be used to add/modify other endpoints.
  node_t *node = node::create(&node_config, app_attribute_update_cb,
                              app_identification_cb);
  ABORT_APP_ON_FAILURE(node != nullptr,
                       ESP_LOGE(TAG, "Failed to create Matter node"));

  MEMORY_PROFILER_DUMP_HEAP_STAT("node created");

  extended_color_light::config_t light_config;
  light_config.on_off.on_off = DEFAULT_POWER;
  light_config.on_off_lighting.start_up_on_off = nullptr;
  light_config.level_control.current_level = DEFAULT_BRIGHTNESS;
  light_config.level_control.on_level = DEFAULT_BRIGHTNESS;
  light_config.level_control_lighting.start_up_current_level =
      DEFAULT_BRIGHTNESS;
  light_config.color_control.color_mode =
      (uint8_t)ColorControl::ColorMode::kColorTemperature;
  light_config.color_control.enhanced_color_mode =
      (uint8_t)ColorControl::ColorMode::kColorTemperature;
  light_config.color_control_color_temperature
      .start_up_color_temperature_mireds = nullptr;

  // endpoint handles can be used to add/modify clusters.
  endpoint_t *endpoint = extended_color_light::create(
      node, &light_config, ENDPOINT_FLAG_NONE, &s_hal);
  ABORT_APP_ON_FAILURE(
      endpoint != nullptr,
      ESP_LOGE(TAG, "Failed to create extended color light endpoint"));

  light_endpoint_id = endpoint::get_id(endpoint);
  ESP_LOGI(TAG, "Light created with endpoint_id %d", light_endpoint_id);

  // Min/Max are applied by the TemperatureMeasurement SCI cluster at init.
  // MeasuredValue itself is owned by the cluster and starts null until
  // SetMeasuredValue().
  temperature_sensor::config_t temperature_config;
  // temperature_config.temperature_measurement.min_measured_value =
  // nullable<int16_t>(0);
  // temperature_config.temperature_measurement.max_measured_value =
  // nullable<int16_t>(50000);
  endpoint_t *temperature_endpoint = temperature_sensor::create(
      node, &temperature_config, ENDPOINT_FLAG_NONE, NULL);
  ABORT_APP_ON_FAILURE(
      temperature_endpoint != nullptr,
      ESP_LOGE(TAG, "Failed to create temperature sensor endpoint"));

  temperature_endpoint_id = endpoint::get_id(temperature_endpoint);
  ESP_LOGI(TAG, "Temperature sensor created with endpoint_id %d",
           temperature_endpoint_id);

  /* Mark deferred persistence for some attributes that might be changed rapidly
   */
  attribute_t *current_level_attribute =
      attribute::get(light_endpoint_id, LevelControl::Id,
                     LevelControl::Attributes::CurrentLevel::Id);
  attribute::set_deferred_persistence(current_level_attribute);

  attribute_t *current_x_attribute =
      attribute::get(light_endpoint_id, ColorControl::Id,
                     ColorControl::Attributes::CurrentX::Id);
  attribute::set_deferred_persistence(current_x_attribute);
  attribute_t *current_y_attribute =
      attribute::get(light_endpoint_id, ColorControl::Id,
                     ColorControl::Attributes::CurrentY::Id);
  attribute::set_deferred_persistence(current_y_attribute);
  attribute_t *color_temp_attribute =
      attribute::get(light_endpoint_id, ColorControl::Id,
                     ColorControl::Attributes::ColorTemperatureMireds::Id);
  attribute::set_deferred_persistence(color_temp_attribute);

#ifdef CONFIG_ENABLE_SET_CERT_DECLARATION_API
  auto *dac_provider = get_dac_provider();
#ifdef CONFIG_SEC_CERT_DAC_PROVIDER
  static_cast<ESP32SecureCertDACProvider *>(dac_provider)
      ->SetCertificationDeclaration(cdSpan);
#elif defined(CONFIG_FACTORY_PARTITION_DAC_PROVIDER)
  static_cast<ESP32FactoryDataProvider *>(dac_provider)
      ->SetCertificationDeclaration(cdSpan);
#endif
#endif // CONFIG_ENABLE_SET_CERT_DECLARATION_API

  /* Matter start */
  err = esp_matter::start(app_event_cb);
  ABORT_APP_ON_FAILURE(err == ESP_OK,
                       ESP_LOGE(TAG, "Failed to start Matter, err:%d", err));

  MEMORY_PROFILER_DUMP_HEAP_STAT("matter started");

  xTaskCreate(MeasureTemperature_Task, "MeasureTemperature_Task", 2 * 1024,
              &s_hal, 3, NULL);

  /* Starting driver with default values */
  app_driver_light_set_defaults(light_endpoint_id);

#if CONFIG_ENABLE_ENCRYPTED_OTA
  err = esp_matter_ota_requestor_encrypted_init(s_decryption_key,
                                                s_decryption_key_len);
  ABORT_APP_ON_FAILURE(
      err == ESP_OK,
      ESP_LOGE(TAG, "Failed to initialized the encrypted OTA, err: %d", err));
#endif // CONFIG_ENABLE_ENCRYPTED_OTA

#if CONFIG_ENABLE_CHIP_SHELL
  esp_matter::console::diagnostics_register_commands();
  esp_matter::console::wifi_register_commands();
  esp_matter::console::factoryreset_register_commands();
  esp_matter::console::attribute_register_commands();
  esp_matter::console::init();
#endif

  while (true) {
    MEMORY_PROFILER_DUMP_HEAP_STAT("Idle");
    vTaskDelay(10000 / portTICK_PERIOD_MS);
  }
}
