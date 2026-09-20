#include "hal.h"

#include <driver/gpio.h>
#include <esp_log.h>
#include <led_strip.h>
#include <max6675.h>

static const char *TAG = "hal_esp_idf";

#define HAL_LED_GPIO GPIO_NUM_5
#define HAL_LED_COUNT 10
#define HAL_LED_RMT_RESOLUTION_HZ 10000000

static led_strip_handle_t s_led_strip;

static esp_err_t hal_esp_idf_init(void)
{
    MAX6675_structure max6675_cfg = {
        .MAX6675_SCK = GPIO_NUM_0,
        .MAX6675_CS = GPIO_NUM_1,
        .MAX6675_MISO = GPIO_NUM_2,
        .TEMPERATURE_CALIBRATION_COEFFICIENT = 0.25f,
    };
    MAX6675_init(max6675_cfg);

    led_strip_config_t strip_config = {
        .strip_gpio_num = HAL_LED_GPIO,
        .max_leds = HAL_LED_COUNT,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags = {
            .invert_out = false,
        },
    };
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = HAL_LED_RMT_RESOLUTION_HZ,
        .mem_block_symbols = 0,
        .flags = {
            .with_dma = false,
        },
    };

    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_led_strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LED strip: %s", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

static esp_err_t hal_esp_idf_set_led(hal_led_color_t color)
{
    if (s_led_strip == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t red = 0;
    uint32_t green = 0;
    uint32_t blue = 0;
    switch (color) {
    case HAL_LED_YELLOW:
        red = 255;
        green = 180;
        break;
    case HAL_LED_GREEN:
        green = 255;
        break;
    default:
        return ESP_ERR_INVALID_ARG;
    }

    for (uint32_t i = 0; i < HAL_LED_COUNT; i++) {
        esp_err_t err = led_strip_set_pixel(s_led_strip, i, red, green, blue);
        if (err != ESP_OK) {
            return err;
        }
    }

    return led_strip_refresh(s_led_strip);
}

static esp_err_t hal_esp_idf_read_temperature(float *celsius)
{
    if (celsius == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *celsius = (float)readCelsius();
    return ESP_OK;
}

static const hal_t s_hal = {
    .init = hal_esp_idf_init,
    .set_led = hal_esp_idf_set_led,
    .read_temperature = hal_esp_idf_read_temperature,
};

const hal_t *hal_esp_idf(void)
{
    return &s_hal;
}
