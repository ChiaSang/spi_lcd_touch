#include "touch_calib_core.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

static uint32_t crc32_step(uint32_t crc, uint8_t data)
{
    crc ^= data;
    for (int i = 0; i < 8; i++) {
        uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
        crc = (crc >> 1) ^ (0xEDB88320U & mask);
    }
    return crc;
}

uint32_t touch_calib_blob_crc32(const touch_calib_blob_t *cal)
{
    touch_calib_blob_t tmp;
    uint32_t crc = 0xFFFFFFFFU;

    if (cal == NULL) {
        return 0;
    }

    tmp = *cal;
    tmp.crc32 = 0;

    const uint8_t *p = (const uint8_t *)&tmp;
    for (size_t i = 0; i < sizeof(tmp); i++) {
        crc = crc32_step(crc, p[i]);
    }

    return ~crc;
}

static esp_err_t solve_3x3(float m[3][3], const float b[3], float out[3])
{
    float a[3][4] = {
        {m[0][0], m[0][1], m[0][2], b[0]},
        {m[1][0], m[1][1], m[1][2], b[1]},
        {m[2][0], m[2][1], m[2][2], b[2]},
    };

    for (int c = 0; c < 3; c++) {
        int pivot = c;
        float max_abs = fabsf(a[c][c]);
        for (int r = c + 1; r < 3; r++) {
            float v = fabsf(a[r][c]);
            if (v > max_abs) {
                max_abs = v;
                pivot = r;
            }
        }

        if (max_abs < 1e-6f) {
            return ESP_ERR_INVALID_STATE;
        }

        if (pivot != c) {
            for (int k = c; k < 4; k++) {
                float t = a[c][k];
                a[c][k] = a[pivot][k];
                a[pivot][k] = t;
            }
        }

        float div = a[c][c];
        for (int k = c; k < 4; k++) {
            a[c][k] /= div;
        }

        for (int r = 0; r < 3; r++) {
            if (r == c) {
                continue;
            }
            float factor = a[r][c];
            for (int k = c; k < 4; k++) {
                a[r][k] -= factor * a[c][k];
            }
        }
    }

    out[0] = a[0][3];
    out[1] = a[1][3];
    out[2] = a[2][3];
    return ESP_OK;
}

esp_err_t touch_calib_solve_affine(const touch_calib_point_t *pts, uint8_t n, touch_calib_blob_t *out)
{
    if (pts == NULL || out == NULL || n < 3) {
        return ESP_ERR_INVALID_ARG;
    }

    float s_xx = 0;
    float s_xy = 0;
    float s_x = 0;
    float s_yy = 0;
    float s_y = 0;
    float s_tx_x = 0;
    float s_ty_x = 0;
    float s_t_x = 0;
    float s_tx_y = 0;
    float s_ty_y = 0;
    float s_t_y = 0;

    for (uint8_t i = 0; i < n; i++) {
        float rx = (float)pts[i].raw_x;
        float ry = (float)pts[i].raw_y;
        float sx = (float)pts[i].scr_x;
        float sy = (float)pts[i].scr_y;

        s_xx += rx * rx;
        s_xy += rx * ry;
        s_x += rx;
        s_yy += ry * ry;
        s_y += ry;

        s_tx_x += rx * sx;
        s_ty_x += ry * sx;
        s_t_x += sx;

        s_tx_y += rx * sy;
        s_ty_y += ry * sy;
        s_t_y += sy;
    }

    float m[3][3] = {
        {s_xx, s_xy, s_x},
        {s_xy, s_yy, s_y},
        {s_x, s_y, (float)n},
    };

    float bx[3] = {s_tx_x, s_ty_x, s_t_x};
    float by[3] = {s_tx_y, s_ty_y, s_t_y};
    float vx[3] = {0};
    float vy[3] = {0};

    esp_err_t err = solve_3x3(m, bx, vx);
    if (err != ESP_OK) {
        return err;
    }

    err = solve_3x3(m, by, vy);
    if (err != ESP_OK) {
        return err;
    }

    out->a = vx[0];
    out->b = vx[1];
    out->c = vx[2];
    out->d = vy[0];
    out->e = vy[1];
    out->f = vy[2];
    out->version = TOUCH_CALIB_VERSION;
    out->crc32 = 0;

    return ESP_OK;
}

void touch_calib_apply(const touch_calib_blob_t *cal, int16_t raw_x, int16_t raw_y, int16_t *out_x, int16_t *out_y)
{
    if (cal == NULL || out_x == NULL || out_y == NULL) {
        return;
    }

    float x = cal->a * raw_x + cal->b * raw_y + cal->c;
    float y = cal->d * raw_x + cal->e * raw_y + cal->f;

    if (x < 0) {
        x = 0;
    }
    if (y < 0) {
        y = 0;
    }
    if (cal->panel_w > 0 && x > (float)(cal->panel_w - 1)) {
        x = (float)(cal->panel_w - 1);
    }
    if (cal->panel_h > 0 && y > (float)(cal->panel_h - 1)) {
        y = (float)(cal->panel_h - 1);
    }

    *out_x = (int16_t)lroundf(x);
    *out_y = (int16_t)lroundf(y);
}

esp_err_t touch_calib_eval(const touch_calib_blob_t *cal, const touch_calib_point_t *pts, uint8_t n, touch_calib_quality_t *q)
{
    if (cal == NULL || pts == NULL || q == NULL || n == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    float sum_sq = 0;
    float max_err = 0;

    for (uint8_t i = 0; i < n; i++) {
        int16_t x = 0;
        int16_t y = 0;
        touch_calib_apply(cal, pts[i].raw_x, pts[i].raw_y, &x, &y);

        float dx = (float)x - (float)pts[i].scr_x;
        float dy = (float)y - (float)pts[i].scr_y;
        float e = sqrtf(dx * dx + dy * dy);
        sum_sq += e * e;
        if (e > max_err) {
            max_err = e;
        }
    }

    q->rmse = sqrtf(sum_sq / n);
    q->max_err = max_err;
    q->used_points = n;
    return ESP_OK;
}

bool touch_calib_blob_is_valid(const touch_calib_blob_t *cal, uint16_t panel_w, uint16_t panel_h)
{
    if (cal == NULL) {
        return false;
    }

    if (cal->version != TOUCH_CALIB_VERSION) {
        return false;
    }

    if (cal->panel_w != panel_w || cal->panel_h != panel_h) {
        return false;
    }

    if (!isfinite(cal->a) || !isfinite(cal->b) || !isfinite(cal->c) ||
        !isfinite(cal->d) || !isfinite(cal->e) || !isfinite(cal->f)) {
        return false;
    }

    return touch_calib_blob_crc32(cal) == cal->crc32;
}
