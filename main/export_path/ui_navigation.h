#ifndef UI_NAVIGATION_H
#define UI_NAVIGATION_H

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_NAV_PAGE_NONE = 0,
    UI_NAV_PAGE_GEAR,
    UI_NAV_PAGE_THEME_GAUGE,
    UI_NAV_PAGE_RPM,
    UI_NAV_PAGE_SPEED,
    UI_NAV_PAGE_TEMP,
    UI_NAV_PAGE_TPMS,
    UI_NAV_PAGE_INFO,
    UI_NAV_PAGE_NEEDLE,
    UI_NAV_PAGE_CHART,
    UI_NAV_PAGE_SKY_GAUGE,
    UI_NAV_PAGE_BLE_SCAN,
    UI_NAV_PAGE_SETTINGS,
    UI_NAV_PAGE_MULTI_GAUGE,
    UI_NAV_PAGE_TEMP_CONFIG,
    UI_NAV_PAGE_TPMS_CONFIG,
    UI_NAV_PAGE_INFO_CONFIG,
    UI_NAV_PAGE_NEEDLE_CONFIG,
    UI_NAV_PAGE_CHART_CONFIG,
    UI_NAV_PAGE_CHART_ALARM,
    UI_NAV_PAGE_OIL_WARN,
    UI_NAV_PAGE_RPM_WARN,
    UI_NAV_PAGE_OBD_PROTOCOL,
    UI_NAV_PAGE_OTA_MODE,
    UI_NAV_PAGE_COUNT
} ui_nav_page_t;

typedef void (*ui_nav_screen_init_cb_t)(void);
typedef void (*ui_nav_leave_cb_t)(void);
typedef void (*ui_nav_fallback_cb_t)(lv_event_t *event, lv_dir_t direction);

void ui_nav_register_page(ui_nav_page_t page, lv_obj_t **screen,
                          ui_nav_screen_init_cb_t screen_init);
void ui_nav_register_leave_cb(ui_nav_page_t page, ui_nav_leave_cb_t callback);
void ui_nav_register_fallback_cb(ui_nav_page_t page, ui_nav_fallback_cb_t callback);
void ui_nav_attach_gesture(lv_obj_t *object, ui_nav_page_t page);
bool ui_nav_show(ui_nav_page_t page, lv_scr_load_anim_t animation,
                 uint32_t duration_ms, uint32_t delay_ms, bool rebuild);
bool ui_nav_show_delete_old(ui_nav_page_t page, lv_scr_load_anim_t animation,
                            uint32_t duration_ms, uint32_t delay_ms);
void ui_nav_gesture_event(lv_event_t *event);

#ifdef __cplusplus
}
#endif

#endif
