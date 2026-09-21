#pragma once

#include <esp_err.h>

#include "types.h"

inline Result from_esp_err(esp_err_t err)
{
    switch (err) {
    case ESP_OK:
        return Result::Ok;
    case ESP_ERR_INVALID_ARG:
        return Result::InvalidArgument;
    case ESP_ERR_INVALID_STATE:
        return Result::NotReady;
    default:
        return Result::Failed;
    }
}

inline esp_err_t to_esp_err(Result status)
{
    switch (status) {
    case Result::Ok:
        return ESP_OK;
    case Result::InvalidArgument:
        return ESP_ERR_INVALID_ARG;
    case Result::NotReady:
        return ESP_ERR_INVALID_STATE;
    case Result::Failed:
    default:
        return ESP_FAIL;
    }
}
