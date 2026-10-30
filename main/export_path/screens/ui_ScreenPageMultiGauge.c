// Dual UART configuration: physical wiring determines left/right identity.
#include "../ui.h"
#include "../ui_navigation.h"
#include "bsp_obd_dsp/nvs_storage.h"
#include "common/gauge_link_protocol.h"
#if CONFIG_GAUGE_FIRMWARE_MASTER
#include "master/gauge_uart_tx.h"
#endif

static lv_obj_t *s_left_value;
static lv_obj_t *s_right_value;
static lv_obj_t *s_picker;
static lv_obj_t *s_picker_title;

static const char *const s_dashboard_names[GAUGE_LINK_DASH_COUNT] = {
    [GAUGE_LINK_DASH_RPM] = "转速", [GAUGE_LINK_DASH_SPEED] = "车速",
    [GAUGE_LINK_DASH_TEMP] = "温度", [GAUGE_LINK_DASH_TPMS] = "胎压",
    [GAUGE_LINK_DASH_FUEL] = "油量", [GAUGE_LINK_DASH_INFO] = "信息",
    [GAUGE_LINK_DASH_NEEDLE] = "指针", [GAUGE_LINK_DASH_CHART] = "图表",
};

static void picker_close(void)
{
    if (s_picker) lv_obj_del(s_picker);
    s_picker = NULL;
    s_picker_title = NULL;
}

static void on_picker_close(lv_event_t *event)
{
    (void)event;
    picker_close();
}

static void on_picker_backdrop(lv_event_t *event)
{
    if (lv_event_get_target(event) == s_picker) picker_close();
}

static void on_dashboard_selected(lv_event_t *event)
{
    uintptr_t option = (uintptr_t)lv_event_get_user_data(event);
    uint8_t target = option >> 8;
    uint8_t dashboard = option & 0xFF;
    if (dashboard >= GAUGE_LINK_DASH_COUNT ||
        (target != GAUGE_LINK_TARGET_LEFT && target != GAUGE_LINK_TARGET_RIGHT)) return;
    nvs_user_cfg_t cfg;
    if (nvs_cfg_get_snapshot(&cfg) != ESP_OK) return;
    uint8_t *id = target == GAUGE_LINK_TARGET_LEFT ? &cfg.left_dashboard_id : &cfg.right_dashboard_id;
    if (*id != dashboard) {
        *id = dashboard;
        if (nvs_cfg_set(&cfg) != ESP_OK) {
            lv_label_set_text(s_picker_title, "保存失败");
            return;
        }
#if CONFIG_GAUGE_FIRMWARE_MASTER
        gauge_uart_tx_send_config_now();
#endif
    }
    lv_label_set_text(target == GAUGE_LINK_TARGET_LEFT ? s_left_value : s_right_value,
                      s_dashboard_names[dashboard]);
    picker_close();
}

static void on_dashboard_clicked(lv_event_t *event)
{
    uint8_t target = (uint8_t)(uintptr_t)lv_event_get_user_data(event);
    nvs_user_cfg_t cfg;
    if (nvs_cfg_get_snapshot(&cfg) != ESP_OK) return;
    uint8_t selected = target == GAUGE_LINK_TARGET_LEFT ? cfg.left_dashboard_id : cfg.right_dashboard_id;
    picker_close();
    s_picker = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_picker, LV_PCT(100), LV_PCT(100));
    lv_obj_center(s_picker);
    lv_obj_set_style_bg_color(s_picker, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_picker, LV_OPA_70, 0);
    lv_obj_set_style_border_width(s_picker, 0, 0);
    lv_obj_set_style_radius(s_picker, 0, 0);
    lv_obj_clear_flag(s_picker, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(s_picker, on_picker_backdrop, LV_EVENT_CLICKED, NULL);
    lv_obj_t *panel = lv_obj_create(s_picker);
    lv_obj_set_size(panel, 264, 242);
    lv_obj_center(panel);
    lv_obj_set_style_bg_color(panel, ui_theme_color_lv(UI_COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_radius(panel, 12, 0);
    lv_obj_set_style_pad_all(panel, 10, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    s_picker_title = lv_label_create(panel);
    lv_label_set_text(s_picker_title, target == GAUGE_LINK_TARGET_LEFT ? "左从表页面" : "右从表页面");
    lv_obj_set_style_text_font(s_picker_title, &ui_font_Chinese16, 0);
    lv_obj_set_style_text_color(s_picker_title, ui_theme_color_lv(UI_COLOR_TEXT_PRIMARY), 0);
    lv_obj_align(s_picker_title, LV_ALIGN_TOP_LEFT, 4, 4);
    lv_obj_t *close = lv_btn_create(panel);
    lv_obj_set_size(close, 26, 26);
    lv_obj_align(close, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_border_width(close, 0, 0);
    lv_obj_t *x = lv_label_create(close);
    lv_label_set_text(x, "X");
    lv_obj_center(x);
    lv_obj_add_event_cb(close, on_picker_close, LV_EVENT_CLICKED, NULL);
    lv_obj_t *list = lv_list_create(panel);
    lv_obj_set_size(list, 242, 182);
    lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_pad_all(list, 4, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(list, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_t *selected_button = NULL;
    for (uint8_t id = 0; id < GAUGE_LINK_DASH_COUNT; ++id) {
        lv_obj_t *button = lv_list_add_btn(list, NULL, s_dashboard_names[id]);
        lv_obj_set_height(button, 34);
        lv_obj_set_style_text_font(button, &ui_font_Chinese16, 0);
        lv_obj_set_style_text_color(button, ui_theme_color_lv(UI_COLOR_TEXT_PRIMARY), 0);
        lv_obj_set_style_bg_color(button, ui_theme_color_lv(UI_COLOR_PANEL), 0);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(button, 0, 0);
        lv_obj_clear_flag(button, LV_OBJ_FLAG_GESTURE_BUBBLE);
        if (id == selected) {
            selected_button = button;
            lv_obj_set_style_bg_color(button, lv_color_hex(0x225599), 0);
            lv_obj_set_style_text_color(button, lv_color_white(), 0);
        }
        lv_obj_add_event_cb(button, on_dashboard_selected, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)((target << 8) | id));
    }
    if (selected_button) lv_obj_scroll_to_view(selected_button, LV_ANIM_OFF);
}

static void on_page_leave(void)
{
    picker_close();
}

static void create_live_tile(lv_obj_t *parent, lv_obj_t **value,
                             const char *name, const char *unit, lv_coord_t x)
{
    lv_obj_t *tile = lv_obj_create(parent);
    lv_obj_remove_style_all(tile);
    lv_obj_set_size(tile, 87, 87);
    lv_obj_set_style_radius(tile, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x0E1718), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_align(tile, LV_ALIGN_CENTER, x, -26);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, name);
    lv_obj_set_style_text_font(label, &ui_font_Chinese16, 0);
    lv_obj_set_style_text_color(label, ui_theme_color_lv(UI_COLOR_ARC_INDICATOR), 0);
    lv_obj_align(label, LV_ALIGN_CENTER, x, -45);
    *value = lv_label_create(parent);
    lv_label_set_text(*value, "--");
    lv_obj_set_style_text_font(*value, &ui_font_FontTypoderSize24, 0);
    lv_obj_set_style_text_color(*value, ui_theme_color_lv(UI_COLOR_TEXT_PRIMARY), 0);
    lv_obj_align(*value, LV_ALIGN_CENTER, x, -21);
    label = lv_label_create(parent);
    lv_label_set_text(label, unit);
    lv_obj_set_style_text_font(label, &ui_font_FontTypoderSize16, 0);
    lv_obj_set_style_text_color(label, ui_theme_color_lv(UI_COLOR_TEXT_SECONDARY), 0);
    lv_obj_align(label, LV_ALIGN_CENTER, x, -1);
}

static lv_obj_t *create_slot(lv_obj_t *parent, const char *name, uint8_t target,
                            uint8_t dashboard, int x)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, name);
    lv_obj_set_style_text_font(label, &ui_font_Chinese16, 0);
    lv_obj_set_style_text_color(label, ui_theme_color_lv(UI_COLOR_TEXT_SECONDARY), 0);
    lv_obj_align(label, LV_ALIGN_CENTER, x, 43);
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_size(button, 130, 32);
    lv_obj_align(button, LV_ALIGN_CENTER, x, 74);
    lv_obj_set_style_bg_color(button, ui_theme_color_lv(UI_COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_GESTURE_BUBBLE);
    label = lv_label_create(button);
    lv_label_set_text(label, s_dashboard_names[dashboard < GAUGE_LINK_DASH_COUNT ? dashboard : 0]);
    lv_obj_set_style_text_font(label, &ui_font_Chinese16, 0);
    lv_obj_set_style_text_color(label, ui_theme_color_lv(UI_COLOR_TEXT_PRIMARY), 0);
    lv_obj_center(label);
    lv_obj_add_event_cb(button, on_dashboard_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)target);
    return label;
}

void ui_ScreenPageMultiGauge_screen_init(void)
{
    const nvs_user_cfg_t *cfg = nvs_cfg_get();
    ui_ScreenPageMultiGauge = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_ScreenPageMultiGauge, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(ui_ScreenPageMultiGauge, 0, 0);
    ui_helpers_style_screen_bg(ui_ScreenPageMultiGauge);
    lv_obj_set_style_bg_opa(ui_ScreenPageMultiGauge, LV_OPA_COVER, 0);
    ui_LabelMultiRole = lv_label_create(ui_ScreenPageMultiGauge);
    lv_label_set_text(ui_LabelMultiRole, "双 UART 多联表");
    lv_obj_set_style_text_font(ui_LabelMultiRole, &ui_font_Chinese16, 0);
    lv_obj_set_style_text_color(ui_LabelMultiRole, ui_theme_color_lv(UI_COLOR_TEXT_PRIMARY), 0);
    lv_obj_align(ui_LabelMultiRole, LV_ALIGN_CENTER, 0, -104);
    create_live_tile(ui_ScreenPageMultiGauge, &ui_LabelMultiValue[0], "转速", "rpm", -94);
    create_live_tile(ui_ScreenPageMultiGauge, &ui_LabelMultiValue[1], "车速", "km/h", 0);
    create_live_tile(ui_ScreenPageMultiGauge, &ui_LabelMultiValue[2], "电压", "V", 94);
    s_left_value = create_slot(ui_ScreenPageMultiGauge, "左从表 / GPIO43", GAUGE_LINK_TARGET_LEFT,
                               cfg->left_dashboard_id, -77);
    s_right_value = create_slot(ui_ScreenPageMultiGauge, "右从表 / GPIO44", GAUGE_LINK_TARGET_RIGHT,
                                cfg->right_dashboard_id, 77);
    lv_obj_t *hint = lv_label_create(ui_ScreenPageMultiGauge);
    lv_label_set_text(hint, "点击配置 · 即时生效\n115200 / 8N1");
    lv_obj_set_style_text_font(hint, &ui_font_Chinese16, 0);
    lv_obj_set_style_text_color(hint, ui_theme_color_lv(UI_COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 124);
    ui_nav_attach_gesture(ui_ScreenPageMultiGauge, UI_NAV_PAGE_MULTI_GAUGE);
    ui_nav_register_leave_cb(UI_NAV_PAGE_MULTI_GAUGE, on_page_leave);
}
