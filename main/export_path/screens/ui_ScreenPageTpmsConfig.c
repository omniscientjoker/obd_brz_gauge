// TPMS pressure and sensor-voltage limits.
// The page is entered by swiping down from the TPMS data page.

#include "../ui.h"
#include "bsp_obd_dsp/nvs_storage.h"

lv_obj_t *ui_ScreenPageTpmsConfig = NULL;

static lv_obj_t *s_min_pressure_slider;
static lv_obj_t *s_max_pressure_slider;
static lv_obj_t *s_voltage_slider;
static lv_obj_t *s_min_pressure_value;
static lv_obj_t *s_max_pressure_value;
static lv_obj_t *s_voltage_value;

static void save_limits(void)
{
    nvs_user_cfg_t cfg = *nvs_cfg_get();
    int32_t min_p = lv_slider_get_value(s_min_pressure_slider);
    int32_t max_p = lv_slider_get_value(s_max_pressure_slider);
    int32_t min_v = lv_slider_get_value(s_voltage_slider);

    cfg.tpms_pressure_min_bar_x100 = (uint16_t)(min_p * 10);
    cfg.tpms_pressure_max_bar_x100 = (uint16_t)(max_p * 10);
    cfg.tpms_voltage_min_mv = (uint16_t)(min_v * 100);
    nvs_cfg_set(&cfg);
}

static void update_limit_labels(void)
{
    if (s_min_pressure_value) {
        lv_label_set_text_fmt(s_min_pressure_value, "%ld.%ld bar",
                              (long)(lv_slider_get_value(s_min_pressure_slider) / 10),
                              (long)(lv_slider_get_value(s_min_pressure_slider) % 10));
    }
    if (s_max_pressure_value) {
        lv_label_set_text_fmt(s_max_pressure_value, "%ld.%ld bar",
                              (long)(lv_slider_get_value(s_max_pressure_slider) / 10),
                              (long)(lv_slider_get_value(s_max_pressure_slider) % 10));
    }
    if (s_voltage_value) {
        lv_label_set_text_fmt(s_voltage_value, "%ld.%ld V",
                              (long)(lv_slider_get_value(s_voltage_slider) / 10),
                              (long)(lv_slider_get_value(s_voltage_slider) % 10));
    }
}

static void on_min_pressure_changed(lv_event_t *e)
{
    LV_UNUSED(e);
    int32_t min_p = lv_slider_get_value(s_min_pressure_slider);
    int32_t max_p = lv_slider_get_value(s_max_pressure_slider);
    if (min_p >= max_p) {
        min_p = max_p - 1;
        if (min_p < 10) min_p = 10;
        lv_slider_set_value(s_min_pressure_slider, min_p, LV_ANIM_OFF);
    }
    update_limit_labels();
    save_limits();
}

static void on_max_pressure_changed(lv_event_t *e)
{
    LV_UNUSED(e);
    int32_t min_p = lv_slider_get_value(s_min_pressure_slider);
    int32_t max_p = lv_slider_get_value(s_max_pressure_slider);
    if (max_p <= min_p) {
        max_p = min_p + 1;
        if (max_p > 40) max_p = 40;
        lv_slider_set_value(s_max_pressure_slider, max_p, LV_ANIM_OFF);
    }
    update_limit_labels();
    save_limits();
}

static void on_voltage_changed(lv_event_t *e)
{
    LV_UNUSED(e);
    update_limit_labels();
    save_limits();
}

static lv_obj_t *create_limit_slider(lv_obj_t *parent, lv_coord_t y,
                                     const char *name, lv_obj_t **value_label)
{
    lv_obj_t *name_label = lv_label_create(parent);
    lv_label_set_text(name_label, name);
    lv_obj_set_style_text_font(name_label, &ui_font_FontTypoderSize16, LV_PART_MAIN);
    lv_obj_set_style_text_color(name_label, lv_color_hex(0xAAAAAA), LV_PART_MAIN);
    lv_obj_align(name_label, LV_ALIGN_CENTER, -102, y);

    lv_obj_t *slider = lv_slider_create(parent);
    lv_obj_set_width(slider, 132);
    lv_obj_set_height(slider, 10);
    lv_obj_align(slider, LV_ALIGN_CENTER, 18, y);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x35CFE0), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 5, LV_PART_KNOB);
    lv_obj_clear_flag(slider, LV_OBJ_FLAG_GESTURE_BUBBLE);

    *value_label = lv_label_create(parent);
    lv_obj_set_width(*value_label, 72);
    lv_obj_set_style_text_align(*value_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_style_text_font(*value_label, &ui_font_FontTypoderSize16, LV_PART_MAIN);
    lv_obj_set_style_text_color(*value_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_align(*value_label, LV_ALIGN_CENTER, 126, y);
    return slider;
}

void ui_ScreenPageTpmsConfig_screen_init(void)
{
    const nvs_user_cfg_t *cfg = nvs_cfg_get();
    int32_t min_p = cfg->tpms_pressure_min_bar_x100 / 10;
    int32_t max_p = cfg->tpms_pressure_max_bar_x100 / 10;
    int32_t min_v = cfg->tpms_voltage_min_mv / 100;
    if (min_p < 10 || min_p > 40) min_p = 20;
    if (max_p <= min_p || max_p > 40) max_p = 32;
    if (min_v < 100 || min_v > 150) min_v = 120;

    ui_ScreenPageTpmsConfig = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_ScreenPageTpmsConfig, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(ui_ScreenPageTpmsConfig, 360, LV_PART_MAIN);
    ui_helpers_style_screen_bg(ui_ScreenPageTpmsConfig);
    lv_obj_set_style_bg_opa(ui_ScreenPageTpmsConfig, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(ui_ScreenPageTpmsConfig, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(ui_ScreenPageTpmsConfig, 0, LV_PART_MAIN);
    lv_obj_set_style_outline_width(ui_ScreenPageTpmsConfig, 0, LV_PART_MAIN);

    ui_helpers_create_ring(ui_ScreenPageTpmsConfig, 8);

    lv_obj_t *title = lv_label_create(ui_ScreenPageTpmsConfig);
    lv_label_set_text(title, "TPMS LIMITS");
    lv_obj_set_style_text_font(title, &ui_font_FontTypoderSize24, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(0x35CFE0), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -112);

    s_min_pressure_slider = create_limit_slider(ui_ScreenPageTpmsConfig, -58,
                                                 "MIN PRESSURE", &s_min_pressure_value);
    s_max_pressure_slider = create_limit_slider(ui_ScreenPageTpmsConfig, 0,
                                                 "MAX PRESSURE", &s_max_pressure_value);
    s_voltage_slider = create_limit_slider(ui_ScreenPageTpmsConfig, 58,
                                            "MIN VOLTAGE", &s_voltage_value);

    lv_slider_set_range(s_min_pressure_slider, 10, 40);
    lv_slider_set_range(s_max_pressure_slider, 10, 40);
    lv_slider_set_range(s_voltage_slider, 100, 150);
    lv_slider_set_value(s_min_pressure_slider, min_p, LV_ANIM_OFF);
    lv_slider_set_value(s_max_pressure_slider, max_p, LV_ANIM_OFF);
    lv_slider_set_value(s_voltage_slider, min_v, LV_ANIM_OFF);
    update_limit_labels();

    lv_obj_t *hint = lv_label_create(ui_ScreenPageTpmsConfig);
    lv_label_set_text(hint, "Swipe up to return");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x666666), LV_PART_MAIN);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 110);

    lv_obj_add_event_cb(s_min_pressure_slider, on_min_pressure_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_max_pressure_slider, on_max_pressure_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_voltage_slider, on_voltage_changed, LV_EVENT_VALUE_CHANGED, NULL);
    ui_nav_attach_gesture(ui_ScreenPageTpmsConfig, UI_NAV_PAGE_TPMS_CONFIG);
}
