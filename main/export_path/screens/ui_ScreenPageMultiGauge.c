// Multi-Gauge (triple-gauge) Settings Page  (entered by swiping down from the settings page)
//  - MODE: MASTER (connects to ELM327 + broadcasts) / SLAVE (receives the master's data) / STANDALONE (standalone, WiFi not started) → NVS device_role
//  - POSITION: this unit's position 1/2/3 (RACE/AS/ONE) in the boot animation     → NVS device_position
//  - INTRO: boot animation: OFF / RACE (LVGL intro) / VIDEO (app-flashed boot_block)  → NVS intro_enable
//  Changes take effect after reboot (next ignition). Swipe up/left/right to return to the settings page.

#include "../ui.h"
#include "bsp_obd_dsp/nvs_storage.h"
#include "bsp_obd_dsp/espnow_link.h"   // ESPNOW_ROLE_STANDALONE

static const char *mode_names = "MASTER\nSLAVE\nALONE";   // index=device_role: 0=MASTER,1=SLAVE,2=STANDALONE
static const char *pos_names  = "1\n2\n3";          // index 0/1/2 → position 1/2/3
static const char *intro_names = "OFF\nRACE\nVIDEO";  // 0=OFF, 1=RACE, 2=VIDEO (boot_block flashed via the phone app)

static lv_obj_t *s_roller_mode = NULL;
static lv_obj_t *s_roller_pos  = NULL;
static lv_obj_t *s_roller_intro = NULL;
static lv_obj_t *s_lbl_pos  = NULL;
static lv_obj_t *s_lbl_intro = NULL;
static lv_obj_t *s_lbl_mode = NULL;
static lv_obj_t *s_role_button = NULL;
static lv_obj_t *s_role_button_label = NULL;

static void create_live_tile(lv_obj_t *parent, lv_obj_t **value_out,
                             const char *name, const char *unit, lv_coord_t x)
{
    lv_obj_t *tile = lv_obj_create(parent);
    lv_obj_remove_style_all(tile);
    lv_obj_set_size(tile, 87, 87);
    lv_obj_set_style_radius(tile, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x0E1718), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(tile, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(tile, lv_color_hex(0x344143), LV_PART_MAIN);
    lv_obj_align(tile, LV_ALIGN_CENTER, x, 9);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *name_label = lv_label_create(parent);
    lv_label_set_text(name_label, name);
    lv_obj_set_width(name_label, 87);
    lv_obj_set_style_text_align(name_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(name_label, &ui_font_FontTypoderSize16, LV_PART_MAIN);
    lv_obj_set_style_text_color(name_label, ui_theme_color_lv(UI_COLOR_ARC_INDICATOR), LV_PART_MAIN);
    lv_obj_align(name_label, LV_ALIGN_CENTER, x, -10);

    *value_out = lv_label_create(parent);
    lv_label_set_text(*value_out, "--");
    lv_obj_set_width(*value_out, 87);
    lv_obj_set_style_text_align(*value_out, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(*value_out, &ui_font_FontTypoderSize24, LV_PART_MAIN);
    lv_obj_set_style_text_color(*value_out, ui_theme_color_lv(UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_obj_align(*value_out, LV_ALIGN_CENTER, x, 14);

    lv_obj_t *unit_label = lv_label_create(parent);
    lv_label_set_text(unit_label, unit);
    lv_obj_set_width(unit_label, 87);
    lv_obj_set_style_text_align(unit_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(unit_label, &ui_font_FontTypoderSize16, LV_PART_MAIN);
    lv_obj_set_style_text_color(unit_label, ui_theme_color_lv(UI_COLOR_TEXT_SECONDARY), LV_PART_MAIN);
    lv_obj_align(unit_label, LV_ALIGN_CENTER, x, 34);
}

// Standalone hides POS, multi-gauge shows all
static void mg_update_visibility(uint8_t role)
{
    bool is_standalone = (role == ESPNOW_ROLE_STANDALONE);
    if (s_lbl_pos)    { if (is_standalone) lv_obj_add_flag(s_lbl_pos, LV_OBJ_FLAG_HIDDEN); else lv_obj_clear_flag(s_lbl_pos, LV_OBJ_FLAG_HIDDEN); }
    if (s_roller_pos) { if (is_standalone) lv_obj_add_flag(s_roller_pos, LV_OBJ_FLAG_HIDDEN); else lv_obj_clear_flag(s_roller_pos, LV_OBJ_FLAG_HIDDEN); }
}

static void on_mode_roller_change(lv_event_t *e)
{
    nvs_user_cfg_t cfg = *nvs_cfg_get();
    cfg.device_role = (uint8_t)lv_roller_get_selected(s_roller_mode);
    nvs_cfg_set(&cfg);
    mg_update_visibility(cfg.device_role);
}
static void on_pos_roller_change(lv_event_t *e)
{
    nvs_device_position_set((uint8_t)lv_roller_get_selected(s_roller_pos) + 1); // index→1/2/3
}
static void on_intro_roller_change(lv_event_t *e)
{
    nvs_intro_enable_set((uint8_t)lv_roller_get_selected(s_roller_intro));
}

static void on_role_button_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    nvs_user_cfg_t cfg = *nvs_cfg_get();
    cfg.device_role = (cfg.device_role == ESPNOW_ROLE_MASTER) ?
                      ESPNOW_ROLE_SLAVE : ESPNOW_ROLE_MASTER;
    nvs_cfg_set(&cfg);
    if (s_role_button_label) {
        lv_label_set_text(s_role_button_label,
                          cfg.device_role == ESPNOW_ROLE_MASTER ? "SET SLAVE" : "SET MASTER");
    }
}

// Shared roller style
static void style_mg_roller(lv_obj_t *r)
{
    lv_obj_clear_flag(r, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_width(r, 92);
    ui_helpers_style_dark_roller(r, &ui_font_FontTypoderSize20);
    lv_roller_set_visible_row_count(r, 1);   // after the font so the row height uses Size20
    lv_obj_set_height(r, 30);                // explicit, same as the settings page rollers
}

static lv_obj_t *make_mg_label(lv_obj_t *parent, const char *txt, int y)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, &ui_font_FontTypoderSize16, LV_PART_MAIN);
    lv_obj_set_style_text_color(l, lv_color_hex(0x888888), LV_PART_MAIN);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, y);
    return l;
}

void ui_ScreenPageMultiGauge_screen_init(void)
{
    const nvs_user_cfg_t *cfg = nvs_cfg_get();

    ui_ScreenPageMultiGauge = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_ScreenPageMultiGauge, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(ui_ScreenPageMultiGauge, 360, LV_PART_MAIN);
    ui_helpers_style_screen_bg(ui_ScreenPageMultiGauge);
    lv_obj_set_style_bg_opa(ui_ScreenPageMultiGauge, 255, LV_PART_MAIN);
    ui_helpers_create_statusbar(ui_ScreenPageMultiGauge, "MULTI");

    // White border ring
    lv_obj_t *ring = ui_helpers_create_ring(ui_ScreenPageMultiGauge, 10);

    // Black ear image at top (created before the widgets so they draw on top of the notch)
    lv_obj_t *ear = lv_img_create(ui_ScreenPageMultiGauge);
    lv_img_set_src(ear, &ui_img_pngblackear_png);
    lv_obj_set_width(ear, LV_SIZE_CONTENT);
    lv_obj_set_height(ear, LV_SIZE_CONTENT);
    lv_obj_set_pos(ear, 0, -142);
    lv_obj_set_align(ear, LV_ALIGN_CENTER);
    lv_obj_add_flag(ear, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(ear, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(ui_ScreenPageMultiGauge);
    lv_label_set_text(title, "MULTI-GAUGE");
    lv_obj_set_style_text_font(title, &ui_font_FontTypoderSize20, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -106);
    lv_obj_add_flag(title, LV_OBJ_FLAG_HIDDEN);

    // Live three-gauge strip mirrors the simulator. Values are filled by the
    // shared OBD snapshot in ui.c; settings remain below and keep their NVS callbacks.
    create_live_tile(ui_ScreenPageMultiGauge, &ui_LabelMultiValue[0], "RPM", "rpm", -94);
    create_live_tile(ui_ScreenPageMultiGauge, &ui_LabelMultiValue[1], "SPEED", "km/h", 0);
    create_live_tile(ui_ScreenPageMultiGauge, &ui_LabelMultiValue[2], "VOLTAGE", "V", 94);
    ui_LabelMultiRole = lv_label_create(ui_ScreenPageMultiGauge);
    lv_label_set_text(ui_LabelMultiRole, "MASTER / MULTI-GAUGE");
    lv_obj_set_width(ui_LabelMultiRole, 280);
    lv_obj_set_style_text_align(ui_LabelMultiRole, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(ui_LabelMultiRole, &ui_font_FontTypoderSize16, LV_PART_MAIN);
    lv_obj_set_style_text_color(ui_LabelMultiRole, ui_theme_color_lv(UI_COLOR_TEXT_SECONDARY), LV_PART_MAIN);
    // The simulator places the role heading immediately above the three
    // circles; the old settings controls remain allocated for compatibility
    // but are hidden from this visual page below.
    lv_obj_align(ui_LabelMultiRole, LV_ALIGN_CENTER, 0, -60);

    s_role_button = lv_btn_create(ui_ScreenPageMultiGauge);
    lv_obj_set_size(s_role_button, 72, 26);
    lv_obj_align(s_role_button, LV_ALIGN_CENTER, -100, 69);
    lv_obj_set_style_radius(s_role_button, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_role_button, lv_color_hex(0x121B1C), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_role_button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_role_button, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_role_button, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_border_opa(s_role_button, 37, LV_PART_MAIN);
    s_role_button_label = lv_label_create(s_role_button);
    lv_obj_set_style_text_font(s_role_button_label, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_role_button_label, lv_color_hex(0xA8B3B4), LV_PART_MAIN);
    lv_label_set_text(s_role_button_label,
                      cfg->device_role == ESPNOW_ROLE_MASTER ? "SET SLAVE" : "SET MASTER");
    lv_obj_center(s_role_button_label);
    lv_obj_add_event_cb(s_role_button, on_role_button_clicked, LV_EVENT_CLICKED, NULL);

    // Row 1: MODE
    s_lbl_mode = make_mg_label(ui_ScreenPageMultiGauge, "MODE", 58);
    s_roller_mode = lv_roller_create(ui_ScreenPageMultiGauge);
    lv_obj_set_style_clip_corner(s_roller_mode, true, 0);
    style_mg_roller(s_roller_mode);
    lv_roller_set_options(s_roller_mode, mode_names, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_selected(s_roller_mode, (cfg->device_role <= 2) ? cfg->device_role : ESPNOW_ROLE_STANDALONE, LV_ANIM_OFF);
    lv_obj_align(s_roller_mode, LV_ALIGN_CENTER, -7, 66);
    lv_obj_add_event_cb(s_roller_mode, on_mode_roller_change, LV_EVENT_VALUE_CHANGED, NULL);

    // Row 2: POS (RACE/AS/ONE position)
    s_lbl_pos = lv_label_create(ui_ScreenPageMultiGauge);
    lv_label_set_text(s_lbl_pos, "POS");
    lv_obj_set_style_text_font(s_lbl_pos, &ui_font_FontTypoderSize16, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lbl_pos, lv_color_hex(0x888888), LV_PART_MAIN);
    lv_obj_align(s_lbl_pos, LV_ALIGN_CENTER, -103, 112);
    s_roller_pos = lv_roller_create(ui_ScreenPageMultiGauge);
    lv_obj_set_style_clip_corner(s_roller_pos, true, 0);
    style_mg_roller(s_roller_pos);
    lv_roller_set_options(s_roller_pos, pos_names, LV_ROLLER_MODE_NORMAL);
    {
        uint8_t p = nvs_device_position_get();
        if (p < 1 || p > 3) p = 1;
        lv_roller_set_selected(s_roller_pos, p - 1, LV_ANIM_OFF);
    }
    lv_obj_align(s_roller_pos, LV_ALIGN_CENTER, -55, 136);
    lv_obj_add_event_cb(s_roller_pos, on_pos_roller_change, LV_EVENT_VALUE_CHANGED, NULL);

    // Row 3: INTRO (multi-gauge: OFF/RACE/VIDEO)
    s_lbl_intro = lv_label_create(ui_ScreenPageMultiGauge);
    lv_label_set_text(s_lbl_intro, "INTRO");
    lv_obj_set_style_text_font(s_lbl_intro, &ui_font_FontTypoderSize16, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lbl_intro, lv_color_hex(0x888888), LV_PART_MAIN);
    lv_obj_align(s_lbl_intro, LV_ALIGN_CENTER, 50, 112);
    s_roller_intro = lv_roller_create(ui_ScreenPageMultiGauge);
    lv_obj_set_style_clip_corner(s_roller_intro, true, 0);
    style_mg_roller(s_roller_intro);
    lv_roller_set_options(s_roller_intro, intro_names, LV_ROLLER_MODE_NORMAL);
    {
        uint8_t ie = nvs_intro_enable_get();
        if (ie > 2) ie = 2;   // legacy REI/SHINJI/ASUKA (3/4) map to VIDEO (2)
        lv_roller_set_selected(s_roller_intro, ie, LV_ANIM_OFF);
    }
    lv_obj_align(s_roller_intro, LV_ALIGN_CENTER, 105, 136);
    lv_obj_add_event_cb(s_roller_intro, on_intro_roller_change, LV_EVENT_VALUE_CHANGED, NULL);

    // Hide irrelevant rows based on role
    mg_update_visibility(cfg->device_role);

    // These selectors belong to the multi-gauge setup workflow, while the
    // simulator's multi page is a compact live-data view with one role action.
    // Keep their callbacks and storage intact for compatibility, but do not
    // let them alter the simulator-matched layout.
    lv_obj_add_flag(s_lbl_mode, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_roller_mode, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_lbl_pos, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_roller_pos, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_lbl_intro, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_roller_intro, LV_OBJ_FLAG_HIDDEN);

    lv_obj_move_foreground(ring);
    lv_obj_add_event_cb(ui_ScreenPageMultiGauge, ui_event_multi_gauge_background, LV_EVENT_GESTURE, NULL);
}
