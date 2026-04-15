#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define TOUCH_CALIB_VERSION 1

typedef struct {
    int16_t raw_x;
    int16_t raw_y;
    int16_t scr_x;
    int16_t scr_y;
} touch_calib_point_t;

typedef struct {
    /* x = a * rx + b * ry + c; y = d * rx + e * ry + f */
    float a;
    float b;
    float c;
    float d;
    float e;
    float f;
    uint16_t panel_w;
    uint16_t panel_h;
    uint8_t version;
    uint32_t crc32;
} touch_calib_blob_t;

typedef struct {
    float rmse;
    float max_err;
    uint8_t used_points;
} touch_calib_quality_t;

esp_err_t touch_calib_solve_affine(const touch_calib_point_t *pts, uint8_t n, touch_calib_blob_t *out);
esp_err_t touch_calib_eval(const touch_calib_blob_t *cal, const touch_calib_point_t *pts, uint8_t n, touch_calib_quality_t *q);
void touch_calib_apply(const touch_calib_blob_t *cal, int16_t raw_x, int16_t raw_y, int16_t *out_x, int16_t *out_y);
bool touch_calib_blob_is_valid(const touch_calib_blob_t *cal, uint16_t panel_w, uint16_t panel_h);
uint32_t touch_calib_blob_crc32(const touch_calib_blob_t *cal);
