#pragma once

#include "lvgl.h"

extern lv_obj_t *ui_ScreenPageUSBSettings;
void ui_usb_settings_screen_init(void);

/* Called with the LVGL mutex held, before normal UI/services are started. */
void ui_usb_storage_show(void);
