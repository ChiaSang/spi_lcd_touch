#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "touch_calib_core.h"

esp_err_t touch_calib_store_init(void);
esp_err_t touch_calib_store_load(touch_calib_blob_t *out, bool *found);
esp_err_t touch_calib_store_save(const touch_calib_blob_t *cal);
esp_err_t touch_calib_store_erase(void);
