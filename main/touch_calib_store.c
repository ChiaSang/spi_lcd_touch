#include "touch_calib_store.h"

#include <string.h>
#include "nvs.h"
#include "nvs_flash.h"

#define TOUCH_CALIB_NVS_NAMESPACE "touch_cal"
#define TOUCH_CALIB_NVS_KEY "affine_v1"

esp_err_t touch_calib_store_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        err = nvs_flash_erase();
        if (err != ESP_OK) {
            return err;
        }
        err = nvs_flash_init();
    }
    return err;
}

esp_err_t touch_calib_store_load(touch_calib_blob_t *out, bool *found)
{
    if (out == NULL || found == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *found = false;
    memset(out, 0, sizeof(*out));

    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open(TOUCH_CALIB_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    size_t required_size = sizeof(*out);
    err = nvs_get_blob(nvs, TOUCH_CALIB_NVS_KEY, out, &required_size);
    nvs_close(nvs);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }

    if (err != ESP_OK) {
        return err;
    }

    if (required_size != sizeof(*out)) {
        memset(out, 0, sizeof(*out));
        return ESP_ERR_INVALID_SIZE;
    }

    *found = true;
    return ESP_OK;
}

esp_err_t touch_calib_store_save(const touch_calib_blob_t *cal)
{
    if (cal == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open(TOUCH_CALIB_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_blob(nvs, TOUCH_CALIB_NVS_KEY, cal, sizeof(*cal));
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }

    nvs_close(nvs);
    return err;
}

esp_err_t touch_calib_store_erase(void)
{
    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open(TOUCH_CALIB_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_erase_key(nvs, TOUCH_CALIB_NVS_KEY);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }

    nvs_close(nvs);
    return err;
}
