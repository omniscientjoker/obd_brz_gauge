// Settings Page
// Configure: default boot page, vehicle, UI theme and screen brightness.
// Theme selection is saved and applied on reboot (esp_restart).
// Swipe navigation is defined centrally in ui_navigation.c.

#include "../ui.h"
#include "bsp_obd_dsp/nvs_storage.h"
#include "bsp_obd_dsp/lcd_driver/ST77916.h"
#include "app_obd_dsp/vehicle_profiles.h"
#include "esp_system.h"

// Page order is shared with the boot router in ui_ext.c.
static const char *const s_boot_page_names[NVS_DEFAULT_PAGE_COUNT] = {
    "温度", "信息", "图表", "仪表", "档位", "转速", "车速", "TPMS", "多联表",
};

typedef enum {
    SETTINGS_PICKER_BOOT_PAGE,
    SETTINGS_PICKER_VEHICLE,
    SETTINGS_PICKER_THEME,
} settings_picker_t;

// Local references for settings widgets and the one active selector overlay.
static lv_obj_t *s_picker_overlay = NULL;
static lv_obj_t *s_label_page_value = NULL;
static lv_obj_t *s_label_vehicle_value = NULL;
static lv_obj_t *s_label_theme_value = NULL;
static lv_obj_t *s_slider_bright = NULL;
static lv_obj_t *s_label_bright_val = NULL;
static lv_obj_t *s_btn_rc = NULL;
static lv_obj_t *s_label_rc = NULL;
static bool s_rc_enabled = false;

static void picker_close(void)
{
    if (s_picker_overlay) {
        lv_obj_del(s_picker_overlay);
        s_picker_overlay = NULL;
    }
}

static void on_picker_close(lv_event_t *e)
{
    (void)e;
    picker_close();
}

static void on_picker_backdrop(lv_event_t *e)
{
    if (lv_event_get_target(e) == s_picker_overlay) picker_close();
}

static void on_page_selected(lv_event_t *e)
{
    uint8_t selected = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    if (selected >= NVS_DEFAULT_PAGE_COUNT) return;
    nvs_user_cfg_t cfg = *nvs_cfg_get();
    cfg.default_page = selected;
    nvs_cfg_set(&cfg);
    lv_label_set_text(s_label_page_value, s_boot_page_names[selected]);
    picker_close();
}

static void on_bright_slider_change(lv_event_t *e)
{
    int32_t val = lv_slider_get_value(s_slider_bright);
    if(val < 10) val = 10;
    lv_label_set_text_fmt(s_label_bright_val, "%ld%%", val);
    nvs_user_cfg_t cfg = *nvs_cfg_get();
    cfg.brightness_day = (uint8_t)val;
    nvs_cfg_set(&cfg);
    Set_Backlight((uint8_t)val);
}

static void on_rc_toggle(lv_event_t *e)
{
    (void)e;
    s_rc_enabled = !s_rc_enabled;
    lv_label_set_text(s_label_rc, s_rc_enabled ? "开" : "关");
    lv_obj_set_style_text_color(s_label_rc,
        s_rc_enabled ? lv_color_hex(0x00CC66) : lv_color_hex(0x888888),
        LV_PART_MAIN);
    nvs_user_cfg_t cfg = *nvs_cfg_get();
    cfg.rc_enabled = s_rc_enabled ? 1 : 0;
    nvs_cfg_set(&cfg);
}

static void on_vehicle_selected(lv_event_t *e)
{
    uint8_t selected = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    uint8_t vehicle_count = 0;
    const vehicle_profile_t *vehicles = vehicle_profile_get_all(&vehicle_count);
    if (!vehicles || selected >= vehicle_count) return;
    // vehicle_profile_set_active clamps out-of-range indices, no need here.
    nvs_user_cfg_t cfg = *nvs_cfg_get();
    cfg.vehicle_profile_idx = selected;
    nvs_cfg_set(&cfg);
    lv_label_set_text(s_label_vehicle_value, vehicles[selected].name);
    // Apply immediately so gear detection etc. picks it up without reboot.
    vehicle_profile_set_active(selected);
    picker_close();
}

// Theme change: persist the selection, then reboot so every screen rebuilds
// with the new theme colors (screens are created once at boot).
static void on_theme_selected(lv_event_t *e)
{
    uint8_t selected = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    if (selected >= ui_theme_count()) return;
    ui_theme_set_active(selected);   // writes theme_cfg.theme to NVS
    esp_restart();
}

static void picker_add_option(lv_obj_t *list, const char *text,
                              lv_event_cb_t callback, uint8_t value)
{
    lv_obj_t *button = lv_list_add_btn(list, NULL, text);
    lv_obj_set_height(button, 34);
    lv_obj_set_style_bg_color(button, ui_theme_color_lv(UI_COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(button, 4, LV_PART_MAIN);
    lv_obj_set_style_text_font(button, &ui_font_Chinese16, LV_PART_MAIN);
    lv_obj_set_style_text_color(button, ui_theme_color_lv(UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, (void *)(uintptr_t)value);
}

static void picker_show(settings_picker_t picker, const char *title)
{
    picker_close();

    s_picker_overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_picker_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_center(s_picker_overlay);
    lv_obj_set_style_bg_color(s_picker_overlay, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_picker_overlay, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_picker_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_picker_overlay, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_picker_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_picker_overlay, on_picker_backdrop, LV_EVENT_CLICKED, NULL);

    lv_obj_t *panel = lv_obj_create(s_picker_overlay);
    lv_obj_set_size(panel, 304, 274);
    lv_obj_center(panel);
    lv_obj_set_style_bg_color(panel, ui_theme_color_lv(UI_COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(panel, ui_theme_color_lv(UI_COLOR_TEXT_SECONDARY), LV_PART_MAIN);
    lv_obj_set_style_radius(panel, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, 8, LV_PART_MAIN);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title_label = lv_label_create(panel);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_font(title_label, &ui_font_Chinese20, LV_PART_MAIN);
    lv_obj_set_style_text_color(title_label, ui_theme_color_lv(UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 4, 1);

    lv_obj_t *close_button = lv_btn_create(panel);
    lv_obj_set_size(close_button, 28, 28);
    lv_obj_align(close_button, LV_ALIGN_TOP_RIGHT, -1, 0);
    lv_obj_set_style_bg_color(close_button, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(close_button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(close_button, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(close_button, 0, LV_PART_MAIN);
    lv_obj_t *close_label = lv_label_create(close_button);
    lv_label_set_text(close_label, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(close_label, ui_theme_color_lv(UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_obj_center(close_label);
    lv_obj_add_event_cb(close_button, on_picker_close, LV_EVENT_CLICKED, NULL);

    lv_obj_t *list = lv_list_create(panel);
    lv_obj_set_size(list, 286, 222);
    lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(list, 4, LV_PART_MAIN);

    if (picker == SETTINGS_PICKER_BOOT_PAGE) {
        for (uint8_t i = 0; i < NVS_DEFAULT_PAGE_COUNT; ++i)
            picker_add_option(list, s_boot_page_names[i], on_page_selected, i);
    } else if (picker == SETTINGS_PICKER_VEHICLE) {
        uint8_t vehicle_count = 0;
        const vehicle_profile_t *vehicles = vehicle_profile_get_all(&vehicle_count);
        for (uint8_t i = 0; vehicles && i < vehicle_count; ++i)
            picker_add_option(list, vehicles[i].name, on_vehicle_selected, i);
    } else {
        for (uint8_t i = 0; i < ui_theme_count(); ++i)
            picker_add_option(list, ui_theme_get(i)->name, on_theme_selected, i);
    }
}

static void on_page_button(lv_event_t *e)
{
    (void)e;
    picker_show(SETTINGS_PICKER_BOOT_PAGE, "启动页");
}

static void on_vehicle_button(lv_event_t *e)
{
    (void)e;
    picker_show(SETTINGS_PICKER_VEHICLE, "车型");
}

static void on_theme_button(lv_event_t *e)
{
    (void)e;
    picker_show(SETTINGS_PICKER_THEME, "主题");
}

static lv_obj_t *create_picker_value_button(lv_obj_t *parent, const char *value,
                                             int32_t y, lv_event_cb_t callback)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_size(button, 150, 28);
    lv_obj_align(button, LV_ALIGN_CENTER, 60, y);
    lv_obj_set_style_bg_color(button, ui_theme_color_lv(UI_COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(button, ui_theme_color_lv(UI_COLOR_TEXT_SECONDARY), LV_PART_MAIN);
    lv_obj_set_style_radius(button, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_left(button, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_right(button, 8, LV_PART_MAIN);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, value);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label, 130);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &ui_font_Chinese16, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, ui_theme_color_lv(UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_obj_center(label);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, NULL);
    return label;
}

void ui_ScreenPageSettings_screen_init(void)
{
    const nvs_user_cfg_t *cfg = nvs_cfg_get();

    ui_ScreenPageSettings = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_ScreenPageSettings, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(ui_ScreenPageSettings, 360, LV_PART_MAIN);
    ui_helpers_style_screen_bg(ui_ScreenPageSettings);
    lv_obj_set_style_bg_opa(ui_ScreenPageSettings, 255, LV_PART_MAIN);


    // Black ear image at top (created before the widgets so the title and
    // rows draw on top of the notch instead of being covered by it)
    lv_obj_t *ear = lv_img_create(ui_ScreenPageSettings);
    lv_img_set_src(ear, &ui_img_pngblackear_png);
    lv_obj_set_width(ear, LV_SIZE_CONTENT);
    lv_obj_set_height(ear, LV_SIZE_CONTENT);
    lv_obj_set_pos(ear, 0, -142);
    lv_obj_set_align(ear, LV_ALIGN_CENTER);
    lv_obj_add_flag(ear, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(ear, LV_OBJ_FLAG_SCROLLABLE);

    // ====== Row 1: Default Page (Boot Page) ======
    lv_obj_t *label_page = lv_label_create(ui_ScreenPageSettings);
    lv_label_set_text(label_page, "启动页");
    lv_obj_set_style_text_font(label_page, &ui_font_Chinese16, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_page, ui_theme_color_lv(UI_COLOR_TEXT_SECONDARY), LV_PART_MAIN);
    lv_obj_align(label_page, LV_ALIGN_CENTER, -82, -64);

    uint8_t boot_page = (cfg->default_page < NVS_DEFAULT_PAGE_COUNT) ? cfg->default_page : 0;
    s_label_page_value = create_picker_value_button(ui_ScreenPageSettings,
                                                     s_boot_page_names[boot_page],
                                                     -64, on_page_button);

    // ====== Row 2: Vehicle ======
    lv_obj_t *label_vehicle = lv_label_create(ui_ScreenPageSettings);
    lv_label_set_text(label_vehicle, "车型");
    lv_obj_set_style_text_font(label_vehicle, &ui_font_Chinese16, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_vehicle, ui_theme_color_lv(UI_COLOR_TEXT_SECONDARY), LV_PART_MAIN);
    lv_obj_align(label_vehicle, LV_ALIGN_CENTER, -82, -30);

    uint8_t vehicle_count = 0;
    const vehicle_profile_t *vehicle_list = vehicle_profile_get_all(&vehicle_count);
    uint8_t vehicle_idx = (vehicle_list && cfg->vehicle_profile_idx < vehicle_count) ?
                          cfg->vehicle_profile_idx : 0;
    const char *vehicle_name = (vehicle_list && vehicle_count > 0) ?
                               vehicle_list[vehicle_idx].name : "OBD2 Generic";
    s_label_vehicle_value = create_picker_value_button(ui_ScreenPageSettings,
                                                        vehicle_name,
                                                        -30, on_vehicle_button);

    // ====== Row 3: UI Theme ======
    lv_obj_t *label_theme = lv_label_create(ui_ScreenPageSettings);
    lv_label_set_text(label_theme, "主题");
    lv_obj_set_style_text_font(label_theme, &ui_font_Chinese16, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_theme, ui_theme_color_lv(UI_COLOR_TEXT_SECONDARY), LV_PART_MAIN);
    lv_obj_align(label_theme, LV_ALIGN_CENTER, -82, 4);

    uint8_t theme_count = ui_theme_count();
    uint8_t theme_idx = (cfg->theme_cfg.theme < theme_count) ? cfg->theme_cfg.theme : 0;
    s_label_theme_value = create_picker_value_button(ui_ScreenPageSettings,
                                                      ui_theme_get(theme_idx)->name,
                                                      4, on_theme_button);

    // ====== Row 4: Brightness ======
    lv_obj_t *label_bright = lv_label_create(ui_ScreenPageSettings);
    lv_label_set_text(label_bright, "亮度");
    lv_obj_set_style_text_font(label_bright, &ui_font_Chinese16, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_bright, ui_theme_color_lv(UI_COLOR_TEXT_SECONDARY), LV_PART_MAIN);
    lv_obj_align(label_bright, LV_ALIGN_CENTER, -82, 38);

    s_slider_bright = lv_slider_create(ui_ScreenPageSettings);
    lv_obj_set_style_clip_corner(s_slider_bright, true, 0);
    lv_slider_set_range(s_slider_bright, 10, 100);
    lv_slider_set_value(s_slider_bright, cfg->brightness_day, LV_ANIM_OFF);
    lv_obj_set_width(s_slider_bright, 80);
    lv_obj_set_height(s_slider_bright, 10);
    lv_obj_align(s_slider_bright, LV_ALIGN_CENTER, 32, 38);
    lv_obj_set_style_bg_color(s_slider_bright, ui_theme_color_lv(UI_COLOR_ARC_TRACK), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_slider_bright, 255, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_slider_bright, ui_theme_color_lv(UI_COLOR_ARC_INDICATOR), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_slider_bright, 255, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_slider_bright, ui_theme_color_lv(UI_COLOR_ARC_INDICATOR), LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_slider_bright, 5, LV_PART_KNOB);
    lv_obj_clear_flag(s_slider_bright, LV_OBJ_FLAG_GESTURE_BUBBLE);      // avoid page swipe while dragging
    lv_obj_add_event_cb(s_slider_bright, on_bright_slider_change, LV_EVENT_VALUE_CHANGED, NULL);

    s_label_bright_val = lv_label_create(ui_ScreenPageSettings);
    lv_label_set_text_fmt(s_label_bright_val, "%d%%", cfg->brightness_day);
    lv_obj_set_style_text_font(s_label_bright_val, &ui_font_FontTypoderSize16, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_label_bright_val, ui_theme_color_lv(UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_obj_align(s_label_bright_val, LV_ALIGN_CENTER, 104, 38);

    // ====== Row 5: RaceChrono Toggle ======
    lv_obj_t *label_rc = lv_label_create(ui_ScreenPageSettings);
    lv_label_set_text(label_rc, "赛道记录");
    lv_obj_set_style_text_font(label_rc, &ui_font_Chinese16, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_rc, ui_theme_color_lv(UI_COLOR_TEXT_SECONDARY), LV_PART_MAIN);
    lv_obj_align(label_rc, LV_ALIGN_CENTER, -82, 76);

    s_rc_enabled = cfg->rc_enabled;
    s_btn_rc = lv_btn_create(ui_ScreenPageSettings);
    lv_obj_set_style_clip_corner(s_btn_rc, true, 0);
    lv_obj_set_size(s_btn_rc, 60, 26);
    lv_obj_align(s_btn_rc, LV_ALIGN_CENTER, 60, 76);
    lv_obj_set_style_bg_color(s_btn_rc, s_rc_enabled ? lv_color_hex(0x00AA55) : lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_btn_rc, 255, LV_PART_MAIN);
    lv_obj_set_style_radius(s_btn_rc, 13, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_btn_rc, 2, LV_PART_MAIN);
    s_label_rc = lv_label_create(s_btn_rc);
    lv_label_set_text(s_label_rc, s_rc_enabled ? "开" : "关");
    lv_obj_set_style_text_font(s_label_rc, &ui_font_Chinese16, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_label_rc, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_center(s_label_rc);
    lv_obj_add_event_cb(s_btn_rc, on_rc_toggle, LV_EVENT_CLICKED, NULL);

    // ====== Hint ======
    lv_obj_t *hint = lv_label_create(ui_ScreenPageSettings);
    lv_label_set_text(hint, "上滑返回 · 下滑多联表");
    lv_obj_set_style_text_font(hint, &ui_font_Chinese16, LV_PART_MAIN);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x555555), LV_PART_MAIN);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 124);

    // Events - swipe to go back / down to multi-gauge
    ui_nav_attach_gesture(ui_ScreenPageSettings, UI_NAV_PAGE_SETTINGS);
}
