#include "ui_navigation.h"

#include <stdint.h>
#include <stddef.h>

#include "ui.h"
#include "theme_engine/theme_interface.h"

typedef struct {
    lv_obj_t **screen;
    ui_nav_screen_init_cb_t init;
    ui_nav_leave_cb_t on_leave;
    ui_nav_fallback_cb_t fallback;
} ui_nav_page_entry_t;

typedef struct {
    ui_nav_page_t source;
    lv_dir_t direction;
    ui_nav_page_t target;
    bool rebuild_target;
} ui_nav_route_t;

static ui_nav_page_entry_t s_pages[UI_NAV_PAGE_COUNT];

static const ui_nav_route_t s_routes[] = {
    { UI_NAV_PAGE_GEAR, LV_DIR_RIGHT, UI_NAV_PAGE_SKY_GAUGE, false },
    { UI_NAV_PAGE_GEAR, LV_DIR_LEFT, UI_NAV_PAGE_RPM, false },
    { UI_NAV_PAGE_GEAR, LV_DIR_BOTTOM, UI_NAV_PAGE_THEME_GAUGE, false },
    { UI_NAV_PAGE_RPM, LV_DIR_RIGHT, UI_NAV_PAGE_GEAR, false },
    { UI_NAV_PAGE_RPM, LV_DIR_LEFT, UI_NAV_PAGE_SPEED, false },
    { UI_NAV_PAGE_RPM, LV_DIR_BOTTOM, UI_NAV_PAGE_RPM_WARN, false },
    { UI_NAV_PAGE_SPEED, LV_DIR_RIGHT, UI_NAV_PAGE_RPM, false },
    { UI_NAV_PAGE_SPEED, LV_DIR_LEFT, UI_NAV_PAGE_TEMP, false },
    { UI_NAV_PAGE_SPEED, LV_DIR_BOTTOM, UI_NAV_PAGE_SPEED_CONFIG, false },
    { UI_NAV_PAGE_TEMP, LV_DIR_RIGHT, UI_NAV_PAGE_SPEED, false },
    { UI_NAV_PAGE_TEMP, LV_DIR_LEFT, UI_NAV_PAGE_TPMS, false },
    { UI_NAV_PAGE_TEMP, LV_DIR_BOTTOM, UI_NAV_PAGE_TEMP_CONFIG, false },
    { UI_NAV_PAGE_TPMS, LV_DIR_RIGHT, UI_NAV_PAGE_TEMP, false },
    { UI_NAV_PAGE_TPMS, LV_DIR_LEFT, UI_NAV_PAGE_INFO, false },
    { UI_NAV_PAGE_TPMS, LV_DIR_BOTTOM, UI_NAV_PAGE_TPMS_CONFIG, false },
    { UI_NAV_PAGE_INFO, LV_DIR_RIGHT, UI_NAV_PAGE_TPMS, false },
    { UI_NAV_PAGE_INFO, LV_DIR_LEFT, UI_NAV_PAGE_NEEDLE, false },
    { UI_NAV_PAGE_INFO, LV_DIR_BOTTOM, UI_NAV_PAGE_INFO_CONFIG, false },
    { UI_NAV_PAGE_NEEDLE, LV_DIR_RIGHT, UI_NAV_PAGE_INFO, false },
    { UI_NAV_PAGE_NEEDLE, LV_DIR_LEFT, UI_NAV_PAGE_CHART, false },
    { UI_NAV_PAGE_NEEDLE, LV_DIR_BOTTOM, UI_NAV_PAGE_NEEDLE_CONFIG, false },
    { UI_NAV_PAGE_CHART, LV_DIR_RIGHT, UI_NAV_PAGE_NEEDLE, false },
    { UI_NAV_PAGE_CHART, LV_DIR_BOTTOM, UI_NAV_PAGE_CHART_CONFIG, true },
    { UI_NAV_PAGE_CHART, LV_DIR_TOP, UI_NAV_PAGE_CHART_ALARM, true },
    { UI_NAV_PAGE_TPMS_CONFIG, LV_DIR_LEFT, UI_NAV_PAGE_TPMS, false },
    { UI_NAV_PAGE_TPMS_CONFIG, LV_DIR_RIGHT, UI_NAV_PAGE_TPMS, false },
    { UI_NAV_PAGE_TPMS_CONFIG, LV_DIR_TOP, UI_NAV_PAGE_TPMS, false },
    { UI_NAV_PAGE_TPMS_CONFIG, LV_DIR_BOTTOM, UI_NAV_PAGE_TPMS, false },
    { UI_NAV_PAGE_TEMP_CONFIG, LV_DIR_LEFT, UI_NAV_PAGE_TEMP, false },
    { UI_NAV_PAGE_TEMP_CONFIG, LV_DIR_RIGHT, UI_NAV_PAGE_TEMP, false },
    { UI_NAV_PAGE_TEMP_CONFIG, LV_DIR_TOP, UI_NAV_PAGE_TEMP, false },
    { UI_NAV_PAGE_INFO_CONFIG, LV_DIR_LEFT, UI_NAV_PAGE_INFO, false },
    { UI_NAV_PAGE_INFO_CONFIG, LV_DIR_RIGHT, UI_NAV_PAGE_INFO, false },
    { UI_NAV_PAGE_INFO_CONFIG, LV_DIR_TOP, UI_NAV_PAGE_INFO, false },
    { UI_NAV_PAGE_NEEDLE_CONFIG, LV_DIR_LEFT, UI_NAV_PAGE_NEEDLE, false },
    { UI_NAV_PAGE_NEEDLE_CONFIG, LV_DIR_RIGHT, UI_NAV_PAGE_NEEDLE, false },
    { UI_NAV_PAGE_NEEDLE_CONFIG, LV_DIR_TOP, UI_NAV_PAGE_NEEDLE, false },
    { UI_NAV_PAGE_NEEDLE_CONFIG, LV_DIR_BOTTOM, UI_NAV_PAGE_NEEDLE, false },
    { UI_NAV_PAGE_CHART_CONFIG, LV_DIR_LEFT, UI_NAV_PAGE_CHART, false },
    { UI_NAV_PAGE_CHART_CONFIG, LV_DIR_RIGHT, UI_NAV_PAGE_CHART, false },
    { UI_NAV_PAGE_CHART_CONFIG, LV_DIR_TOP, UI_NAV_PAGE_CHART, false },
    { UI_NAV_PAGE_CHART_CONFIG, LV_DIR_BOTTOM, UI_NAV_PAGE_CHART, false },
    { UI_NAV_PAGE_CHART_ALARM, LV_DIR_LEFT, UI_NAV_PAGE_CHART, false },
    { UI_NAV_PAGE_CHART_ALARM, LV_DIR_RIGHT, UI_NAV_PAGE_CHART, false },
    { UI_NAV_PAGE_CHART_ALARM, LV_DIR_TOP, UI_NAV_PAGE_CHART, false },
    { UI_NAV_PAGE_CHART_ALARM, LV_DIR_BOTTOM, UI_NAV_PAGE_CHART, false },
    { UI_NAV_PAGE_OIL_WARN, LV_DIR_LEFT, UI_NAV_PAGE_CHART, false },
    { UI_NAV_PAGE_OIL_WARN, LV_DIR_RIGHT, UI_NAV_PAGE_CHART, false },
    { UI_NAV_PAGE_OIL_WARN, LV_DIR_TOP, UI_NAV_PAGE_CHART, false },
    { UI_NAV_PAGE_RPM_WARN, LV_DIR_LEFT, UI_NAV_PAGE_RPM, false },
    { UI_NAV_PAGE_RPM_WARN, LV_DIR_RIGHT, UI_NAV_PAGE_RPM, false },
    { UI_NAV_PAGE_RPM_WARN, LV_DIR_TOP, UI_NAV_PAGE_RPM, false },
    { UI_NAV_PAGE_SPEED_CONFIG, LV_DIR_LEFT, UI_NAV_PAGE_SPEED, false },
    { UI_NAV_PAGE_SPEED_CONFIG, LV_DIR_RIGHT, UI_NAV_PAGE_SPEED, false },
    { UI_NAV_PAGE_SPEED_CONFIG, LV_DIR_TOP, UI_NAV_PAGE_SPEED, false },
    { UI_NAV_PAGE_SPEED_CONFIG, LV_DIR_BOTTOM, UI_NAV_PAGE_SPEED, false },
    // Sky Gauge is the left-most page: right returns to Gear, while left has
    // no route and is intentionally ignored.
    { UI_NAV_PAGE_SKY_GAUGE, LV_DIR_RIGHT, UI_NAV_PAGE_GEAR, false },
    { UI_NAV_PAGE_SKY_GAUGE, LV_DIR_TOP, UI_NAV_PAGE_BLE_SCAN, false },
    { UI_NAV_PAGE_SKY_GAUGE, LV_DIR_BOTTOM, UI_NAV_PAGE_SETTINGS, false },
    { UI_NAV_PAGE_BLE_SCAN, LV_DIR_BOTTOM, UI_NAV_PAGE_SKY_GAUGE, false },
    { UI_NAV_PAGE_SETTINGS, LV_DIR_TOP, UI_NAV_PAGE_SKY_GAUGE, false },
    { UI_NAV_PAGE_SETTINGS, LV_DIR_BOTTOM, UI_NAV_PAGE_ALERT_MEDIA, false },
    { UI_NAV_PAGE_ALERT_MEDIA, LV_DIR_TOP, UI_NAV_PAGE_SETTINGS, false },
    { UI_NAV_PAGE_ALERT_MEDIA, LV_DIR_BOTTOM, UI_NAV_PAGE_MULTI_GAUGE, false },
    { UI_NAV_PAGE_MULTI_GAUGE, LV_DIR_LEFT, UI_NAV_PAGE_SETTINGS, false },
    { UI_NAV_PAGE_MULTI_GAUGE, LV_DIR_RIGHT, UI_NAV_PAGE_SETTINGS, false },
    { UI_NAV_PAGE_MULTI_GAUGE, LV_DIR_TOP, UI_NAV_PAGE_ALERT_MEDIA, false },
    { UI_NAV_PAGE_OBD_PROTOCOL, LV_DIR_LEFT, UI_NAV_PAGE_TEMP, false },
    { UI_NAV_PAGE_OBD_PROTOCOL, LV_DIR_RIGHT, UI_NAV_PAGE_TEMP, false },
};

static ui_nav_page_t current_page(void)
{
    lv_obj_t *active = lv_scr_act();
    for (ui_nav_page_t page = UI_NAV_PAGE_GEAR; page < UI_NAV_PAGE_COUNT; page++) {
        if (s_pages[page].screen && *s_pages[page].screen == active) return page;
    }
    return UI_NAV_PAGE_NONE;
}

void ui_nav_register_page(ui_nav_page_t page, lv_obj_t **screen,
                          ui_nav_screen_init_cb_t screen_init)
{
    if (page <= UI_NAV_PAGE_NONE || page >= UI_NAV_PAGE_COUNT) return;
    s_pages[page].screen = screen;
    s_pages[page].init = screen_init;
}

void ui_nav_register_leave_cb(ui_nav_page_t page, ui_nav_leave_cb_t callback)
{
    if (page <= UI_NAV_PAGE_NONE || page >= UI_NAV_PAGE_COUNT) return;
    s_pages[page].on_leave = callback;
}

void ui_nav_register_fallback_cb(ui_nav_page_t page, ui_nav_fallback_cb_t callback)
{
    if (page <= UI_NAV_PAGE_NONE || page >= UI_NAV_PAGE_COUNT) return;
    s_pages[page].fallback = callback;
}

void ui_nav_attach_gesture(lv_obj_t *object, ui_nav_page_t page)
{
    if (!object || page <= UI_NAV_PAGE_NONE || page >= UI_NAV_PAGE_COUNT) return;
    lv_obj_add_event_cb(object, ui_nav_gesture_event, LV_EVENT_GESTURE,
                        (void *)(uintptr_t)page);
}

static bool show_page(ui_nav_page_t page, lv_scr_load_anim_t animation,
                      uint32_t duration_ms, uint32_t delay_ms, bool rebuild,
                      bool delete_previous)
{
    if (page <= UI_NAV_PAGE_NONE || page >= UI_NAV_PAGE_COUNT) return false;
    ui_nav_page_entry_t *entry = &s_pages[page];
    if (!entry->screen || !entry->init) return false;

    lv_obj_t *old_active = lv_scr_act();
    ui_nav_page_t old_page = current_page();
    bool same_page = old_page == page;
    if (old_page != UI_NAV_PAGE_NONE && !same_page && s_pages[old_page].on_leave) {
        s_pages[old_page].on_leave();
    }

    bool delete_old_active = delete_previous;
    if (rebuild && *entry->screen) {
        if (*entry->screen == old_active) {
            *entry->screen = NULL;
            delete_old_active = true;
        } else {
            lv_obj_del(*entry->screen);
            *entry->screen = NULL;
        }
    }
    if (!*entry->screen) entry->init();
    if (!*entry->screen) return false;

    lv_scr_load_anim(*entry->screen, animation, duration_ms, delay_ms,
                     delete_old_active);
    return true;
}

bool ui_nav_show(ui_nav_page_t page, lv_scr_load_anim_t animation,
                 uint32_t duration_ms, uint32_t delay_ms, bool rebuild)
{
    return show_page(page, animation, duration_ms, delay_ms, rebuild, false);
}

bool ui_nav_show_delete_old(ui_nav_page_t page, lv_scr_load_anim_t animation,
                            uint32_t duration_ms, uint32_t delay_ms)
{
    return show_page(page, animation, duration_ms, delay_ms, false, true);
}

static bool show_theme_page(ui_nav_page_t source, lv_dir_t direction)
{
    uint8_t count = theme_page_list_count();
    if (count == 0) return false;

    if (source == UI_NAV_PAGE_GEAR) {
        ui_theme_gauge_page_index = 0;
    } else if (source == UI_NAV_PAGE_SKY_GAUGE) {
        ui_theme_gauge_page_index = count - 1;
    } else if (direction == LV_DIR_LEFT) {
        if (ui_theme_gauge_page_index + 1 >= count) {
            return ui_nav_show(UI_NAV_PAGE_SKY_GAUGE, LV_SCR_LOAD_ANIM_FADE_ON, 5, 0, false);
        }
        ui_theme_gauge_page_index++;
    } else if (direction == LV_DIR_RIGHT) {
        if (ui_theme_gauge_page_index == 0) {
            return ui_nav_show(UI_NAV_PAGE_SKY_GAUGE, LV_SCR_LOAD_ANIM_FADE_ON, 5, 0, false);
        }
        ui_theme_gauge_page_index--;
    }

    return ui_nav_show(UI_NAV_PAGE_THEME_GAUGE, LV_SCR_LOAD_ANIM_FADE_ON, 5, 0,
                       true);
}

static bool dispatch_route(ui_nav_page_t source, lv_dir_t direction)
{
    if ((source == UI_NAV_PAGE_GEAR && direction == LV_DIR_BOTTOM) ||
        (source == UI_NAV_PAGE_THEME_GAUGE &&
         (direction == LV_DIR_LEFT || direction == LV_DIR_RIGHT))) {
        if (show_theme_page(source, direction)) return true;
        if (source == UI_NAV_PAGE_GEAR) return false;
        return ui_nav_show(UI_NAV_PAGE_SKY_GAUGE, LV_SCR_LOAD_ANIM_FADE_ON,
                           5, 0, false);
    }

    for (size_t i = 0; i < sizeof(s_routes) / sizeof(s_routes[0]); i++) {
        const ui_nav_route_t *route = &s_routes[i];
        if (route->source == source && route->direction == direction) {
            return ui_nav_show(route->target, LV_SCR_LOAD_ANIM_FADE_ON, 5, 0,
                               route->rebuild_target);
        }
    }
    return false;
}

void ui_nav_gesture_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_GESTURE) return;
    ui_nav_page_t page = (ui_nav_page_t)(uintptr_t)lv_event_get_user_data(event);
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;
    lv_dir_t direction = lv_indev_get_gesture_dir(indev);
    lv_indev_wait_release(indev);
    bool handled = dispatch_route(page, direction);
    if (!handled && page > UI_NAV_PAGE_NONE && page < UI_NAV_PAGE_COUNT &&
        s_pages[page].fallback) {
        s_pages[page].fallback(event, direction);
        handled = true;
    }
    if (handled) {
        lv_event_stop_bubbling(event);
    }
}
