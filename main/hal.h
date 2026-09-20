#pragma once

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HAL_LED_YELLOW,
    HAL_LED_GREEN,
} hal_led_color_t;

typedef struct {
    esp_err_t (*init)(void);
    esp_err_t (*set_led)(hal_led_color_t color);
    esp_err_t (*read_temperature)(float *celsius);
} hal_t;

const hal_t *hal_esp_idf(void);

#ifdef __cplusplus
}
#endif
