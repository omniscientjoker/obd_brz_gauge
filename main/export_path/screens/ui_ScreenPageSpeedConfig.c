#include "../ui.h"
#include "bsp_obd_dsp/nvs_storage.h"

static lv_obj_t *s_slider;
static lv_obj_t *s_value;

static void on_speed_max_changed(lv_event_t *e)
{
    (void)e;
    int32_t value = lv_slider_get_value(s_slider);
    value = ((value + 5) / 10) * 10;
    if (value < NVS_SPEED_MAX_MIN_KMH) value = NVS_SPEED_MAX_MIN_KMH;
    if (value > NVS_SPEED_MAX_MAX_KMH) value = NVS_SPEED_MAX_MAX_KMH;
    lv_slider_set_value(s_slider, value, LV_ANIM_OFF);
    lv_label_set_text_fmt(s_value, "%ld km/h", (long)value);

    nvs_speed_max_kmh_set((uint16_t)value);
}

void ui_ScreenPageSpeedConfig_screen_init(void)
{
    uint16_t max_speed = nvs_speed_max_kmh_get();
    if (max_speed < NVS_SPEED_MAX_MIN_KMH || max_speed > NVS_SPEED_MAX_MAX_KMH)
        max_speed = NVS_SPEED_MAX_DEFAULT_KMH;

    ui_ScreenPageSpeedConfig = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_ScreenPageSpeedConfig, LV_OBJ_FLAG_SCROLLABLE);
    ui_helpers_style_screen_bg(ui_ScreenPageSpeedConfig);
    lv_obj_set_style_bg_opa(ui_ScreenPageSpeedConfig, 255, LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(ui_ScreenPageSpeedConfig);
    lv_label_set_text(title, "SPEED MAX");
    lv_obj_set_style_text_font(title, &ui_font_FontTypoderSize24, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, ui_theme_color_lv(UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -92);

    s_value = lv_label_create(ui_ScreenPageSpeedConfig);
    lv_label_set_text_fmt(s_value, "%u km/h", (unsigned)max_speed);
    lv_obj_set_style_text_font(s_value, &ui_font_FontTypoderSize44, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_value, ui_theme_color_lv(UI_COLOR_ARC_INDICATOR), LV_PART_MAIN);
    lv_obj_align(s_value, LV_ALIGN_CENTER, 0, -28);

    s_slider = lv_slider_create(ui_ScreenPageSpeedConfig);
    lv_slider_set_range(s_slider, NVS_SPEED_MAX_MIN_KMH, NVS_SPEED_MAX_MAX_KMH);
    lv_slider_set_value(s_slider, max_speed, LV_ANIM_OFF);
    lv_obj_set_width(s_slider, 240);
    lv_obj_set_height(s_slider, 14);
    lv_obj_align(s_slider, LV_ALIGN_CENTER, 0, 32);
    lv_obj_set_style_bg_color(s_slider, ui_theme_color_lv(UI_COLOR_ARC_TRACK), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_slider, 255, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_slider, ui_theme_color_lv(UI_COLOR_ARC_INDICATOR), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_slider, 255, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_slider, ui_theme_color_lv(UI_COLOR_ARC_INDICATOR), LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_slider, 6, LV_PART_KNOB);
    lv_obj_clear_flag(s_slider, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(s_slider, on_speed_max_changed, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *range = lv_label_create(ui_ScreenPageSpeedConfig);
    lv_label_set_text(range, "160 - 300 km/h");
    lv_obj_set_style_text_font(range, &ui_font_FontTypoderSize16, LV_PART_MAIN);
    lv_obj_set_style_text_color(range, ui_theme_color_lv(UI_COLOR_TEXT_SECONDARY), LV_PART_MAIN);
    lv_obj_align(range, LV_ALIGN_CENTER, 0, 68);

    lv_obj_t *hint = lv_label_create(ui_ScreenPageSpeedConfig);
    lv_label_set_text(hint, "上滑返回速度页面");
    lv_obj_set_style_text_font(hint, &ui_font_Chinese16, LV_PART_MAIN);
    lv_obj_set_style_text_color(hint, ui_theme_color_lv(UI_COLOR_TEXT_SECONDARY), LV_PART_MAIN);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 118);

    ui_nav_attach_gesture(ui_ScreenPageSpeedConfig, UI_NAV_PAGE_SPEED_CONFIG);
}
