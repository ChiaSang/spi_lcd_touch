idf_component_register(
    SRCS "touch_calib_core.c"
         "touch_calib_store.c"
         "touch_calib_wizard.c"
         "touch_calib_manager.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash
    PRIV_REQUIRES lvgl__lvgl espressif__esp_lcd_touch
)
