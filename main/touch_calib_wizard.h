#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_lcd_touch.h"
#include "lvgl.h"
#include "touch_calib_core.h"

typedef enum {
    TOUCH_CALIB_WIZARD_IDLE = 0,
    TOUCH_CALIB_WIZARD_SHOW_TARGET,
    TOUCH_CALIB_WIZARD_WAIT_PRESS,
    TOUCH_CALIB_WIZARD_SAMPLE,
    TOUCH_CALIB_WIZARD_WAIT_RELEASE,
    TOUCH_CALIB_WIZARD_SOLVE,
    TOUCH_CALIB_WIZARD_DONE,
    TOUCH_CALIB_WIZARD_FAILED,
} touch_calib_wizard_state_t;

typedef struct {
    lv_display_t *display;
    esp_lcd_touch_handle_t touch;
    uint16_t panel_w;
    uint16_t panel_h;
    uint8_t points_count;
    uint8_t samples_per_point;
    float max_err_px;
} touch_calib_wizard_cfg_t;

esp_err_t touch_calib_wizard_init(const touch_calib_wizard_cfg_t *cfg);
void touch_calib_wizard_start(void);
void touch_calib_wizard_stop(void);
void touch_calib_wizard_step(void);
bool touch_calib_wizard_is_running(void);
bool touch_calib_wizard_fetch_result(touch_calib_blob_t *out);
touch_calib_wizard_state_t touch_calib_wizard_get_state(void);
const char *touch_calib_wizard_state_name(touch_calib_wizard_state_t state);
