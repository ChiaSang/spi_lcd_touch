#include "touch_calib_wizard.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"

static const char *TAG_WIZ = "calib_wizard";

/* ---- Stability / quality knobs ---- */
#define STABLE_PRESS_THRESHOLD  4   /* consecutive reads with finger down */
#define STABLE_TOLERANCE_PX     3   /* max raw drift between reads */
#define OUTLIER_DRIFT_PX        8   /* skip sample if this far from running avg */

/* ---- Target visual sizes (pixels) ---- */
#define TARGET_RING_RADIUS      18
#define TARGET_CROSS_LEN        14  /* half-length of crosshair lines */

typedef struct {
    bool inited;
    bool running;
    bool result_ready;
    touch_calib_wizard_cfg_t cfg;
    touch_calib_wizard_state_t state;

    lv_obj_t *overlay;
    lv_obj_t *ring;          /* circle outline */
    lv_obj_t *line_h;        /* horizontal crosshair */
    lv_obj_t *line_v;        /* vertical crosshair */
    lv_obj_t *tip_label;

    touch_calib_point_t points[5];
    uint8_t target_count;
    uint8_t target_index;
    uint8_t stable_press_count;
    uint8_t sample_count;
    uint8_t outlier_count;
    int32_t sample_sum_x;
    int32_t sample_sum_y;
    int16_t last_raw_x;      /* for stability check */
    int16_t last_raw_y;

    touch_calib_blob_t result;
} touch_calib_wizard_ctx_t;

static touch_calib_wizard_ctx_t s_wiz = {0};

static const int16_t s_target_percent_3[3][2] = {
    {15, 15},
    {85, 50},
    {15, 85},
};

static const int16_t s_target_percent_5[5][2] = {
    {12, 12},
    {88, 12},
    {88, 88},
    {12, 88},
    {50, 50},
};

static void wizard_cleanup_overlay(void)
{
    if (s_wiz.overlay) {
        lv_obj_delete(s_wiz.overlay);
        s_wiz.overlay = NULL;
    }
    s_wiz.ring = NULL;
    s_wiz.line_h = NULL;
    s_wiz.line_v = NULL;
    s_wiz.tip_label = NULL;
}

static void wizard_create_overlay(void)
{
    lv_obj_t *screen = lv_screen_active();

    /* Full-screen dark translucent overlay */
    s_wiz.overlay = lv_obj_create(screen);
    lv_obj_remove_style_all(s_wiz.overlay);
    lv_obj_set_size(s_wiz.overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_wiz.overlay, lv_color_hex(0x101820), 0);
    lv_obj_set_style_bg_opa(s_wiz.overlay, LV_OPA_80, 0);
    lv_obj_set_style_border_width(s_wiz.overlay, 0, 0);
    lv_obj_add_flag(s_wiz.overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_wiz.overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(s_wiz.overlay);

    /* Ring (circle outline) */
    s_wiz.ring = lv_obj_create(s_wiz.overlay);
    lv_obj_remove_style_all(s_wiz.ring);
    lv_obj_set_size(s_wiz.ring, TARGET_RING_RADIUS * 2, TARGET_RING_RADIUS * 2);
    lv_obj_set_style_radius(s_wiz.ring, TARGET_RING_RADIUS, 0);
    lv_obj_set_style_border_color(s_wiz.ring, lv_color_hex(0xF4D35E), 0);
    lv_obj_set_style_border_width(s_wiz.ring, 2, 0);
    lv_obj_set_style_border_opa(s_wiz.ring, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(s_wiz.ring, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_wiz.ring, LV_OBJ_FLAG_SCROLLABLE);

    /* Horizontal crosshair line */
    static lv_point_precise_t h_points[2] = { {-TARGET_CROSS_LEN, 0}, {TARGET_CROSS_LEN, 0} };
    s_wiz.line_h = lv_line_create(s_wiz.overlay);
    lv_line_set_points(s_wiz.line_h, h_points, 2);
    lv_obj_set_style_line_color(s_wiz.line_h, lv_color_hex(0xF4D35E), 0);
    lv_obj_set_style_line_width(s_wiz.line_h, 2, 0);
    lv_obj_clear_flag(s_wiz.line_h, LV_OBJ_FLAG_SCROLLABLE);

    /* Vertical crosshair line */
    static lv_point_precise_t v_points[2] = { {0, -TARGET_CROSS_LEN}, {0, TARGET_CROSS_LEN} };
    s_wiz.line_v = lv_line_create(s_wiz.overlay);
    lv_line_set_points(s_wiz.line_v, v_points, 2);
    lv_obj_set_style_line_color(s_wiz.line_v, lv_color_hex(0xF4D35E), 0);
    lv_obj_set_style_line_width(s_wiz.line_v, 2, 0);
    lv_obj_clear_flag(s_wiz.line_v, LV_OBJ_FLAG_SCROLLABLE);

    /* Tip label at bottom */
    s_wiz.tip_label = lv_label_create(s_wiz.overlay);
    lv_obj_set_style_text_color(s_wiz.tip_label, lv_color_hex(0xE7ECEF), 0);
    lv_label_set_long_mode(s_wiz.tip_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_wiz.tip_label, s_wiz.cfg.panel_w - 16);
    lv_obj_align(s_wiz.tip_label, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_clear_flag(s_wiz.tip_label, LV_OBJ_FLAG_SCROLLABLE);
}

static void wizard_target_screen_xy(uint8_t index, int16_t *x, int16_t *y)
{
    int16_t px = 50;
    int16_t py = 50;

    if (s_wiz.target_count == 3) {
        px = s_target_percent_3[index][0];
        py = s_target_percent_3[index][1];
    } else {
        px = s_target_percent_5[index][0];
        py = s_target_percent_5[index][1];
    }

    *x = (int16_t)((s_wiz.cfg.panel_w - 1) * px / 100);
    *y = (int16_t)((s_wiz.cfg.panel_h - 1) * py / 100);
}

static void wizard_show_target(void)
{
    int16_t x = 0;
    int16_t y = 0;
    char text[96] = {0};

    wizard_target_screen_xy(s_wiz.target_index, &x, &y);

    ESP_LOGI(TAG_WIZ, "Show target %u/%u at screen (%d, %d)",
             (unsigned)(s_wiz.target_index + 1), (unsigned)s_wiz.target_count, x, y);

    /* Position the ring centered on target */
    lv_obj_set_pos(s_wiz.ring, x - TARGET_RING_RADIUS, y - TARGET_RING_RADIUS);

    /* Position crosshair lines centered on target */
    lv_obj_set_pos(s_wiz.line_h, x, y);
    lv_obj_set_pos(s_wiz.line_v, x, y);

    snprintf(text, sizeof(text), "Calibration %u/%u: tap and hold the target",
             (unsigned)(s_wiz.target_index + 1), (unsigned)s_wiz.target_count);
    lv_label_set_text(s_wiz.tip_label, text);
}

static bool wizard_read_raw(uint16_t *x, uint16_t *y)
{
    esp_lcd_touch_point_data_t pt = {0};
    uint8_t cnt = 0;

    esp_lcd_touch_read_data(s_wiz.cfg.touch);
    if (esp_lcd_touch_get_data(s_wiz.cfg.touch, &pt, &cnt, 1) != ESP_OK || cnt == 0) {
        return false;
    }

    *x = pt.x;
    *y = pt.y;
    return true;
}

static void wizard_reset_point_sampling(void)
{
    s_wiz.stable_press_count = 0;
    s_wiz.sample_count = 0;
    s_wiz.outlier_count = 0;
    s_wiz.sample_sum_x = 0;
    s_wiz.sample_sum_y = 0;
    s_wiz.last_raw_x = -1;
    s_wiz.last_raw_y = -1;
}

esp_err_t touch_calib_wizard_init(const touch_calib_wizard_cfg_t *cfg)
{
    if (cfg == NULL || cfg->display == NULL || cfg->touch == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (cfg->points_count != 3 && cfg->points_count != 5) {
        return ESP_ERR_INVALID_ARG;
    }

    if (cfg->samples_per_point == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(&s_wiz, 0, sizeof(s_wiz));
    s_wiz.cfg = *cfg;
    s_wiz.target_count = cfg->points_count;
    s_wiz.state = TOUCH_CALIB_WIZARD_IDLE;
    s_wiz.inited = true;
    s_wiz.last_raw_x = -1;
    s_wiz.last_raw_y = -1;
    ESP_LOGI(TAG_WIZ, "Init: %upoints, %usamples/pt, max_err=%.1fpx, panel=%ux%u",
             (unsigned)cfg->points_count, (unsigned)cfg->samples_per_point,
             cfg->max_err_px, (unsigned)cfg->panel_w, (unsigned)cfg->panel_h);
    return ESP_OK;
}

void touch_calib_wizard_start(void)
{
    if (!s_wiz.inited) {
        ESP_LOGW(TAG_WIZ, "Start called but not inited");
        return;
    }

    wizard_cleanup_overlay();
    wizard_create_overlay();

    memset(s_wiz.points, 0, sizeof(s_wiz.points));
    s_wiz.target_index = 0;
    s_wiz.result_ready = false;
    s_wiz.running = true;
    s_wiz.state = TOUCH_CALIB_WIZARD_SHOW_TARGET;
    wizard_reset_point_sampling();
    ESP_LOGI(TAG_WIZ, "Wizard started");
}

void touch_calib_wizard_stop(void)
{
    if (!s_wiz.inited) {
        return;
    }

    ESP_LOGI(TAG_WIZ, "Wizard stopped in state %s", touch_calib_wizard_state_name(s_wiz.state));
    s_wiz.running = false;
    s_wiz.state = TOUCH_CALIB_WIZARD_IDLE;
    wizard_cleanup_overlay();
}

void touch_calib_wizard_step(void)
{
    if (!s_wiz.running) {
        return;
    }

    uint16_t rx = 0;
    uint16_t ry = 0;
    bool pressed = false;

    switch (s_wiz.state) {
    case TOUCH_CALIB_WIZARD_SHOW_TARGET:
        wizard_show_target();
        s_wiz.state = TOUCH_CALIB_WIZARD_WAIT_PRESS;
        break;

    case TOUCH_CALIB_WIZARD_WAIT_PRESS:
        pressed = wizard_read_raw(&rx, &ry);
        if (pressed) {
            /* Check coordinate stability */
            if (s_wiz.last_raw_x >= 0 && s_wiz.last_raw_y >= 0) {
                int16_t dx = (int16_t)rx - s_wiz.last_raw_x;
                int16_t dy = (int16_t)ry - s_wiz.last_raw_y;
                if (abs(dx) > STABLE_TOLERANCE_PX || abs(dy) > STABLE_TOLERANCE_PX) {
                    /* Too much drift, reset stability counter */
                    s_wiz.stable_press_count = 0;
                }
            }
            s_wiz.last_raw_x = (int16_t)rx;
            s_wiz.last_raw_y = (int16_t)ry;
            s_wiz.stable_press_count++;
            if (s_wiz.stable_press_count >= STABLE_PRESS_THRESHOLD) {
                ESP_LOGI(TAG_WIZ, "Press stable at raw (%u, %u) after %u reads -> SAMPLING",
                         (unsigned)rx, (unsigned)ry, (unsigned)s_wiz.stable_press_count);
                s_wiz.state = TOUCH_CALIB_WIZARD_SAMPLE;
                s_wiz.sample_count = 0;
                s_wiz.outlier_count = 0;
                s_wiz.sample_sum_x = 0;
                s_wiz.sample_sum_y = 0;
            }
        } else {
            s_wiz.stable_press_count = 0;
            s_wiz.last_raw_x = -1;
            s_wiz.last_raw_y = -1;
        }
        break;

    case TOUCH_CALIB_WIZARD_SAMPLE:
        pressed = wizard_read_raw(&rx, &ry);
        if (!pressed) {
            ESP_LOGW(TAG_WIZ, "Finger lifted after %u samples (outliers: %u) -> back to WAIT_PRESS",
                     (unsigned)s_wiz.sample_count, (unsigned)s_wiz.outlier_count);
            s_wiz.state = TOUCH_CALIB_WIZARD_WAIT_PRESS;
            wizard_reset_point_sampling();
            break;
        }

        /* Outlier rejection: skip sample if too far from running average */
        if (s_wiz.sample_count >= 2) {
            int32_t avg_x = s_wiz.sample_sum_x / s_wiz.sample_count;
            int32_t avg_y = s_wiz.sample_sum_y / s_wiz.sample_count;
            int16_t dx = (int16_t)((int32_t)rx - avg_x);
            int16_t dy = (int16_t)((int32_t)ry - avg_y);
            if (abs(dx) > OUTLIER_DRIFT_PX || abs(dy) > OUTLIER_DRIFT_PX) {
                s_wiz.outlier_count++;
                ESP_LOGD(TAG_WIZ, "Outlier #%u: raw=(%u,%u) avg=(%ld,%ld) dx=%d dy=%d",
                         (unsigned)s_wiz.outlier_count, (unsigned)rx, (unsigned)ry,
                         avg_x, avg_y, dx, dy);
                break; /* skip this sample */
            }
        }

        s_wiz.sample_sum_x += rx;
        s_wiz.sample_sum_y += ry;
        s_wiz.sample_count++;

        ESP_LOGD(TAG_WIZ, "Sample %u/%u raw=(%u,%u)",
                 (unsigned)s_wiz.sample_count, (unsigned)s_wiz.cfg.samples_per_point,
                 (unsigned)rx, (unsigned)ry);

        if (s_wiz.sample_count >= s_wiz.cfg.samples_per_point) {
            int16_t sx = 0;
            int16_t sy = 0;
            wizard_target_screen_xy(s_wiz.target_index, &sx, &sy);

            int16_t avg_rx = (int16_t)(s_wiz.sample_sum_x / s_wiz.sample_count);
            int16_t avg_ry = (int16_t)(s_wiz.sample_sum_y / s_wiz.sample_count);

            s_wiz.points[s_wiz.target_index].raw_x = avg_rx;
            s_wiz.points[s_wiz.target_index].raw_y = avg_ry;
            s_wiz.points[s_wiz.target_index].scr_x = sx;
            s_wiz.points[s_wiz.target_index].scr_y = sy;

            ESP_LOGI(TAG_WIZ, "Point %u captured: raw=(%d,%d) screen=(%d,%d) outliers=%u",
                     (unsigned)(s_wiz.target_index + 1), avg_rx, avg_ry, sx, sy,
                     (unsigned)s_wiz.outlier_count);

            lv_label_set_text(s_wiz.tip_label, "Point captured! Release touch...");
            s_wiz.state = TOUCH_CALIB_WIZARD_WAIT_RELEASE;
        }
        break;

    case TOUCH_CALIB_WIZARD_WAIT_RELEASE:
        pressed = wizard_read_raw(&rx, &ry);
        if (!pressed) {
            s_wiz.target_index++;
            wizard_reset_point_sampling();
            if (s_wiz.target_index < s_wiz.target_count) {
                s_wiz.state = TOUCH_CALIB_WIZARD_SHOW_TARGET;
            } else {
                s_wiz.state = TOUCH_CALIB_WIZARD_SOLVE;
            }
        }
        break;

    case TOUCH_CALIB_WIZARD_SOLVE: {
        ESP_LOGI(TAG_WIZ, "Solving affine with %u points...", (unsigned)s_wiz.target_count);
        for (uint8_t i = 0; i < s_wiz.target_count; i++) {
            ESP_LOGI(TAG_WIZ, "  pt[%u] raw=(%d,%d) scr=(%d,%d)",
                     i, s_wiz.points[i].raw_x, s_wiz.points[i].raw_y,
                     s_wiz.points[i].scr_x, s_wiz.points[i].scr_y);
        }

        touch_calib_blob_t solved = {0};
        touch_calib_quality_t q = {0};
        esp_err_t err = touch_calib_solve_affine(s_wiz.points, s_wiz.target_count, &solved);
        if (err != ESP_OK) {
            ESP_LOGE(TAG_WIZ, "Affine solve failed (err=0x%x)", err);
            lv_label_set_text(s_wiz.tip_label, "Solve FAILED. Tap to retry.");
            s_wiz.state = TOUCH_CALIB_WIZARD_FAILED;
            break;
        }

        solved.panel_w = s_wiz.cfg.panel_w;
        solved.panel_h = s_wiz.cfg.panel_h;
        solved.version = TOUCH_CALIB_VERSION;
        solved.crc32 = touch_calib_blob_crc32(&solved);

        ESP_LOGI(TAG_WIZ, "Affine: a=%.4f b=%.4f c=%.1f d=%.4f e=%.4f f=%.1f",
                 solved.a, solved.b, solved.c, solved.d, solved.e, solved.f);

        err = touch_calib_eval(&solved, s_wiz.points, s_wiz.target_count, &q);
        if (err != ESP_OK) {
            ESP_LOGE(TAG_WIZ, "Eval failed (err=0x%x)", err);
            lv_label_set_text(s_wiz.tip_label, "Eval FAILED. Tap to retry.");
            s_wiz.state = TOUCH_CALIB_WIZARD_FAILED;
            break;
        }

        ESP_LOGI(TAG_WIZ, "Quality: rmse=%.2f max_err=%.2f (limit=%.1f)",
                 q.rmse, q.max_err, s_wiz.cfg.max_err_px);

        if (q.max_err > s_wiz.cfg.max_err_px) {
            ESP_LOGW(TAG_WIZ, "Max error %.2f exceeds limit %.1f", q.max_err, s_wiz.cfg.max_err_px);
            lv_label_set_text(s_wiz.tip_label, "Error too high! Tap to retry.");
            s_wiz.state = TOUCH_CALIB_WIZARD_FAILED;
            break;
        }

        s_wiz.result = solved;
        s_wiz.result_ready = true;
        ESP_LOGI(TAG_WIZ, "Calibration DONE successfully!");
        lv_label_set_text(s_wiz.tip_label, "Calibration OK!");
        s_wiz.state = TOUCH_CALIB_WIZARD_DONE;
        s_wiz.running = false;
        wizard_cleanup_overlay();
        break;
    }

    case TOUCH_CALIB_WIZARD_DONE:
        s_wiz.running = false;
        wizard_cleanup_overlay();
        break;

    case TOUCH_CALIB_WIZARD_FAILED:
        pressed = wizard_read_raw(&rx, &ry);
        if (pressed) {
            s_wiz.target_index = 0;
            wizard_reset_point_sampling();
            s_wiz.state = TOUCH_CALIB_WIZARD_SHOW_TARGET;
        }
        break;

    case TOUCH_CALIB_WIZARD_IDLE:
    default:
        break;
    }
}

bool touch_calib_wizard_is_running(void)
{
    return s_wiz.running;
}

bool touch_calib_wizard_fetch_result(touch_calib_blob_t *out)
{
    if (!s_wiz.result_ready || out == NULL) {
        return false;
    }

    *out = s_wiz.result;
    s_wiz.result_ready = false;
    return true;
}

touch_calib_wizard_state_t touch_calib_wizard_get_state(void)
{
    return s_wiz.state;
}

const char *touch_calib_wizard_state_name(touch_calib_wizard_state_t state)
{
    switch (state) {
    case TOUCH_CALIB_WIZARD_IDLE:
        return "IDLE";
    case TOUCH_CALIB_WIZARD_SHOW_TARGET:
        return "SHOW_TARGET";
    case TOUCH_CALIB_WIZARD_WAIT_PRESS:
        return "WAIT_PRESS";
    case TOUCH_CALIB_WIZARD_SAMPLE:
        return "SAMPLE";
    case TOUCH_CALIB_WIZARD_WAIT_RELEASE:
        return "WAIT_RELEASE";
    case TOUCH_CALIB_WIZARD_SOLVE:
        return "SOLVE";
    case TOUCH_CALIB_WIZARD_DONE:
        return "DONE";
    case TOUCH_CALIB_WIZARD_FAILED:
        return "FAILED";
    default:
        return "UNKNOWN";
    }
}
