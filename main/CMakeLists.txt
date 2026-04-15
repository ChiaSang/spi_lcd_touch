idf_component_register(SRCS "spi_lcd_touch_example_main.c" "lvgl_demo_ui.c" "touch_calib_core.c" "touch_calib_store.c" "touch_calib_wizard.c"
                       INCLUDE_DIRS "."
                       REQUIRES nvs_flash)
