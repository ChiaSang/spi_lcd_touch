/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

// This demo UI is adapted from LVGL official example: https://docs.lvgl.io/master/examples.html#loader-with-arc

#include "lvgl.h"
#include <stdio.h>

static lv_obj_t * btn;
static lv_obj_t * label_coords;  // 触摸坐标显示标签
static lv_display_rotation_t rotation = LV_DISP_ROTATION_0;

static void btn_cb(lv_event_t * e)
{
    lv_display_t *disp = lv_event_get_user_data(e);
    rotation++;
    if (rotation > LV_DISP_ROTATION_270) {
        rotation = LV_DISP_ROTATION_0;
    }
    lv_disp_set_rotation(disp, rotation);
}

/**
 * @brief 全屏触摸测试 - 触摸任意位置显示坐标
 */
static void touch_test_cb(lv_event_t * e)
{
    lv_point_t p;
    lv_indev_get_point(lv_indev_get_act(), &p);
    
    // 更新坐标显示
    static char buf[64];
    snprintf(buf, sizeof(buf), "X:%ld Y:%ld", (int32_t)p.x, (int32_t)p.y);
    lv_label_set_text(label_coords, buf);
}

static void set_angle(void * obj, int32_t v)
{
    lv_arc_set_value(obj, v);
}

void example_lvgl_demo_ui(lv_display_t *disp)
{
    lv_obj_t *scr = lv_display_get_screen_active(disp);

    // 创建一个全屏透明的可点击区域用于调试触摸坐标
    lv_obj_t * touch_test = lv_obj_create(scr);
    lv_obj_set_size(touch_test, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(touch_test, 0, 0);
    lv_obj_set_style_bg_opa(touch_test, 0, 0);  // 完全透明
    lv_obj_set_style_border_opa(touch_test, 0, 0);
    lv_obj_add_event_cb(touch_test, touch_test_cb, LV_EVENT_CLICKED, NULL);

    // 坐标显示标签 - 左上角
    label_coords = lv_label_create(scr);
    lv_label_set_text(label_coords, "Touch anywhere!");
    lv_obj_set_style_text_font(label_coords, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(label_coords, lv_color_white(), 0);
    lv_obj_set_style_bg_color(label_coords, lv_color_make(0, 0, 128), 0);
    lv_obj_set_style_bg_opa(label_coords, 200, 0);
    lv_obj_set_pos(label_coords, 10, 10);

    btn = lv_button_create(scr);
    lv_obj_t * lbl = lv_label_create(btn);
    lv_label_set_text_static(lbl, LV_SYMBOL_REFRESH" ROTATE");
    lv_obj_align(btn, LV_ALIGN_BOTTOM_LEFT, 30, -30);
    /*Button event*/
    lv_obj_add_event_cb(btn, btn_cb, LV_EVENT_CLICKED, disp);

    /*Create an Arc*/
    lv_obj_t * arc = lv_arc_create(scr);
    lv_arc_set_rotation(arc, 270);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);   /*Be sure the knob is not displayed*/
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);  /*To not allow adjusting by click*/
    lv_obj_center(arc);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, arc);
    lv_anim_set_exec_cb(&a, set_angle);
    lv_anim_set_duration(&a, 1000);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);    /*Just for the demo*/
    lv_anim_set_repeat_delay(&a, 500);
    lv_anim_set_values(&a, 0, 100);
    lv_anim_start(&a);
}
