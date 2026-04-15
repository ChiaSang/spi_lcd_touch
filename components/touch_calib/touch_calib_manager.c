#include "touch_calib_manager.h"
#include "touch_calib_store.h"
#include "touch_calib_wizard.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "touch_calib_mgr";

typedef struct {
    bool inited;
    bool calibrated;
    touch_calib_blob_t calibration;
    touch_calib_blob_t fallback;
    bool has_fallback;
    touch_calib_mgr_config_t cfg;
    volatile bool calib_requested;
    volatile bool erase_requested;
} calib_mgr_t;

static calib_mgr_t s_mgr;

esp_err_t touch_calib_mgr_init(const touch_calib_mgr_config_t *cfg)
{
    if (cfg == NULL || cfg->display == NULL || cfg->touch == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(&s_mgr, 0, sizeof(s_mgr));
    s_mgr.cfg = *cfg;

    esp_err_t err = touch_calib_store_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS init failed: %s", esp_err_to_name(err));
        return err;
    }
    s_mgr.inited = true;
    return ESP_OK;
}

esp_err_t touch_calib_mgr_load_calibration(void)
{
    if (!s_mgr.inited) {
        return ESP_ERR_INVALID_STATE;
    }
    touch_calib_blob_t loaded = {0};
    bool found = false;
    esp_err_t err = touch_calib_store_load(&loaded, &found);
    if (err == ESP_OK && found &&
        touch_calib_blob_is_valid(&loaded, s_mgr.cfg.panel_w, s_mgr.cfg.panel_h)) {
        s_mgr.calibration = loaded;
        s_mgr.calibrated = true;
        ESP_LOGI(TAG, "Calibration loaded from NVS");
        return ESP_OK;
    }
    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "Calibration load failed: %s", esp_err_to_name(err));
    }
    return ESP_ERR_NOT_FOUND;
}

void touch_calib_mgr_set_fallback(const touch_calib_blob_t *fallback)
{
    if (fallback == NULL) {
        return;
    }
    s_mgr.fallback = *fallback;
    s_mgr.has_fallback = true;
    if (!s_mgr.calibrated) {
        s_mgr.calibration = s_mgr.fallback;
        s_mgr.calibrated = true;
        ESP_LOGW(TAG, "Using fallback calibration");
    }
}

esp_err_t touch_calib_mgr_save_calibration(const touch_calib_blob_t *cal)
{
    if (cal == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = touch_calib_store_save(cal);
    if (err == ESP_OK) {
        s_mgr.calibration = *cal;
        s_mgr.calibrated = true;
        ESP_LOGI(TAG, "Calibration saved to NVS");
    }
    return err;
}

esp_err_t touch_calib_mgr_erase_calibration(void)
{
    esp_err_t err = touch_calib_store_erase();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Calibration erased from NVS");
    }
    if (s_mgr.has_fallback) {
        s_mgr.calibration = s_mgr.fallback;
        s_mgr.calibrated = true;
    } else {
        memset(&s_mgr.calibration, 0, sizeof(s_mgr.calibration));
        s_mgr.calibrated = false;
    }
    return err;
}

void touch_calib_mgr_apply(int16_t raw_x, int16_t raw_y,
                           int16_t *out_x, int16_t *out_y)
{
    if (!s_mgr.calibrated) {
        *out_x = raw_x;
        *out_y = raw_y;
        return;
    }
    touch_calib_apply(&s_mgr.calibration, raw_x, raw_y, out_x, out_y);
}

bool touch_calib_mgr_is_calibrated(void)
{
    return s_mgr.calibrated;
}

esp_err_t touch_calib_mgr_start_wizard(void)
{
    if (!s_mgr.inited) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t points = (s_mgr.cfg.calib_points == 3) ? 3 : 5;
    touch_calib_wizard_cfg_t wiz_cfg = {
        .display            = s_mgr.cfg.display,
        .touch              = s_mgr.cfg.touch,
        .panel_w            = s_mgr.cfg.panel_w,
        .panel_h            = s_mgr.cfg.panel_h,
        .points_count       = points,
        .samples_per_point  = s_mgr.cfg.samples_per_point,
        .max_err_px         = s_mgr.cfg.max_err_px,
    };
    esp_err_t err = touch_calib_wizard_init(&wiz_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wizard init failed: %s", esp_err_to_name(err));
        return err;
    }
    s_mgr.calibrated = false;
    touch_calib_wizard_start();
    ESP_LOGI(TAG, "Calibration wizard started");
    return ESP_OK;
}

void touch_calib_mgr_stop_wizard(void)
{
    touch_calib_wizard_stop();
    /* Restore fallback if we lost calibration during wizard */
    if (!s_mgr.calibrated && s_mgr.has_fallback) {
        s_mgr.calibration = s_mgr.fallback;
        s_mgr.calibrated = true;
        ESP_LOGW(TAG, "Wizard stopped, restored fallback calibration");
    }
}

bool touch_calib_mgr_process(void)
{
    if (!s_mgr.inited) {
        return false;
    }

    /* Handle deferred erase request */
    if (s_mgr.erase_requested) {
        s_mgr.erase_requested = false;
        touch_calib_mgr_erase_calibration();
        s_mgr.calib_requested = true;
    }

    /* Handle deferred wizard-start request */
    if (s_mgr.calib_requested && !touch_calib_wizard_is_running()) {
        s_mgr.calib_requested = false;
        touch_calib_mgr_start_wizard();
    }

    /* Step the wizard state machine */
    touch_calib_wizard_step();

    /* Fetch result when the wizard finishes */
    touch_calib_blob_t solved = {0};
    if (touch_calib_wizard_fetch_result(&solved)) {
        esp_err_t err = touch_calib_mgr_save_calibration(&solved);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Calibration save failed: %s", esp_err_to_name(err));
            if (s_mgr.has_fallback) {
                s_mgr.calibration = s_mgr.fallback;
                s_mgr.calibrated = true;
            }
        }
        return true;
    }
    return false;
}

bool touch_calib_mgr_wizard_running(void)
{
    return touch_calib_wizard_is_running();
}

const char *touch_calib_mgr_wizard_state_name(void)
{
    return touch_calib_wizard_state_name(touch_calib_wizard_get_state());
}

void touch_calib_mgr_request_calib(void)
{
    s_mgr.calib_requested = true;
    ESP_LOGI(TAG, "Calibration requested");
}

void touch_calib_mgr_request_erase(void)
{
    s_mgr.erase_requested = true;
    ESP_LOGI(TAG, "Calibration erase requested");
}
