#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_lcd_touch.h"
#include "lvgl.h"
#include "touch_calib_core.h"

/**
 * @brief Configuration for the touch calibration manager.
 */
typedef struct {
    uint16_t panel_w;              /**< Panel width in pixels. */
    uint16_t panel_h;              /**< Panel height in pixels. */
    lv_display_t *display;         /**< LVGL display instance. */
    esp_lcd_touch_handle_t touch;  /**< Touch controller handle. */
    uint8_t calib_points;          /**< Calibration target count (3 or 5). */
    uint8_t samples_per_point;     /**< Raw samples per target (3-20). */
    float max_err_px;              /**< Max acceptable fitting error (px). */
} touch_calib_mgr_config_t;

/**
 * @brief Initialize the calibration manager and NVS storage.
 */
esp_err_t touch_calib_mgr_init(const touch_calib_mgr_config_t *cfg);

/**
 * @brief Try to load a valid calibration from NVS.
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if no valid data.
 */
esp_err_t touch_calib_mgr_load_calibration(void);

/**
 * @brief Set a fallback/legacy calibration blob.
 *        Used when NVS has no valid data yet.
 */
void touch_calib_mgr_set_fallback(const touch_calib_blob_t *fallback);

/**
 * @brief Save a calibration blob to NVS and activate it.
 */
esp_err_t touch_calib_mgr_save_calibration(const touch_calib_blob_t *cal);

/**
 * @brief Erase calibration from NVS and revert to fallback.
 */
esp_err_t touch_calib_mgr_erase_calibration(void);

/**
 * @brief Apply current calibration to raw coordinates.
 *        If not calibrated, passes through unchanged.
 */
void touch_calib_mgr_apply(int16_t raw_x, int16_t raw_y,
                           int16_t *out_x, int16_t *out_y);

/**
 * @brief Check whether a valid calibration is active.
 */
bool touch_calib_mgr_is_calibrated(void);

/**
 * @brief Start the interactive calibration wizard.
 */
esp_err_t touch_calib_mgr_start_wizard(void);

/**
 * @brief Stop the wizard.
 */
void touch_calib_mgr_stop_wizard(void);

/**
 * @brief Main-loop tick: steps the wizard, handles pending requests,
 *        and fetches the result when the wizard completes.
 * @return true when a new calibration was just produced.
 */
bool touch_calib_mgr_process(void);

/** @return true while the wizard is actively running. */
bool touch_calib_mgr_wizard_running(void);

/** @return Human-readable wizard state name. */
const char *touch_calib_mgr_wizard_state_name(void);

/** @brief Request a new calibration wizard run (processed on next tick). */
void touch_calib_mgr_request_calib(void);

/** @brief Request erase of stored calibration (processed on next tick). */
void touch_calib_mgr_request_erase(void);
