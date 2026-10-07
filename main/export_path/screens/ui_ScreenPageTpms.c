// TPMS and ECU voltage page. Tire pressures are supplied by the vehicle
// protocol layer and rendered here without changing the page layout.

#include "../ui.h"

lv_obj_t *ui_LabelTpmsValue[4] = {NULL, NULL, NULL, NULL};
lv_obj_t *ui_LabelTpmsVoltage = NULL;
lv_obj_t *ui_LabelTpmsHeader = NULL;
lv_obj_t *ui_TpmsCard[4] = {NULL, NULL, NULL, NULL};

static void create_tire_tile(lv_obj_t *parent, int index, const char *name,
                             lv_coord_t x, lv_coord_t y)
{
    // The simulator uses four fixed circular cards. Keep the wheel labels and
    // values as independent LVGL objects so the protocol updater can continue
    // changing only the value labels.
    lv_obj_t *card = lv_obj_create(parent);
    ui_TpmsCard[index] = card;
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 116, 116);
    lv_obj_set_style_radius(card, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x0B1F2F), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(0x35CFE0), LV_PART_MAIN);
    lv_obj_set_style_shadow_width(card, 12, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(card, lv_color_hex(0x25CDE3), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(card, LV_OPA_20, LV_PART_MAIN);
    lv_obj_align(card, LV_ALIGN_CENTER, x, y);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *inner = lv_obj_create(parent);
    lv_obj_remove_style_all(inner);
    lv_obj_set_size(inner, 86, 86);
    lv_obj_set_style_radius(inner, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(inner, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(inner, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(inner, lv_color_hex(UI_EDGE_RING_COLOR), LV_PART_MAIN);
    lv_obj_set_style_border_opa(inner, 64, LV_PART_MAIN);
    lv_obj_align(inner, LV_ALIGN_CENTER, x, y);
    lv_obj_clear_flag(inner, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, name);
    lv_obj_set_style_text_font(label, &ui_font_FontTypoderSize16, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(0x5CAEFF), LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_CENTER, x, y - 27);

    ui_LabelTpmsValue[index] = lv_label_create(parent);
    lv_label_set_text(ui_LabelTpmsValue[index], "--.-");
    lv_obj_set_width(ui_LabelTpmsValue[index], 120);
    lv_obj_set_style_text_align(ui_LabelTpmsValue[index], LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(ui_LabelTpmsValue[index], &ui_font_FontTypoderSize36, LV_PART_MAIN);
    lv_obj_set_style_text_color(ui_LabelTpmsValue[index], lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_align(ui_LabelTpmsValue[index], LV_ALIGN_CENTER, x, y + 2);

    lv_obj_t *unit = lv_label_create(parent);
    lv_label_set_text(unit, "bar");
    lv_obj_set_style_text_font(unit, &ui_font_FontTypoderSize16, LV_PART_MAIN);
    lv_obj_set_style_text_color(unit, lv_color_hex(0x777777), LV_PART_MAIN);
    lv_obj_align(unit, LV_ALIGN_CENTER, x, y + 30);
}

void ui_ScreenPageTpms_screen_init(void)
{
    ui_ScreenPageTpms = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_ScreenPageTpms, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(ui_ScreenPageTpms, 360, LV_PART_MAIN);
    ui_helpers_style_screen_bg(ui_ScreenPageTpms);
    lv_obj_set_style_bg_opa(ui_ScreenPageTpms, 255, LV_PART_MAIN);


    ui_LabelTpmsHeader = lv_label_create(ui_ScreenPageTpms);
    lv_label_set_text(ui_LabelTpmsHeader, "2.0-3.2 bar");
    lv_obj_set_width(ui_LabelTpmsHeader, 150);
    lv_obj_set_style_text_align(ui_LabelTpmsHeader, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(ui_LabelTpmsHeader, &ui_font_FontTypoderSize16, LV_PART_MAIN);
    lv_obj_set_style_text_color(ui_LabelTpmsHeader, lv_color_hex(0x35CFE0), LV_PART_MAIN);
    lv_obj_align(ui_LabelTpmsHeader, LV_ALIGN_CENTER, 0, -145);

    create_tire_tile(ui_ScreenPageTpms, 0, "FL", -101, -101);
    create_tire_tile(ui_ScreenPageTpms, 1, "FR",  101, -101);
    create_tire_tile(ui_ScreenPageTpms, 2, "RL", -101,  101);
    create_tire_tile(ui_ScreenPageTpms, 3, "RR",  101,  101);

    lv_obj_t *voltage_name = lv_label_create(ui_ScreenPageTpms);
    lv_label_set_text(voltage_name, "电压");
    lv_obj_set_style_text_font(voltage_name, &ui_font_Chinese16, LV_PART_MAIN);
    lv_obj_set_style_text_color(voltage_name, lv_color_hex(0xE0B85A), LV_PART_MAIN);
    lv_obj_align(voltage_name, LV_ALIGN_CENTER, 0, -9);

    ui_LabelTpmsVoltage = lv_label_create(ui_ScreenPageTpms);
    lv_label_set_text(ui_LabelTpmsVoltage, "--.- V");
    lv_obj_set_width(ui_LabelTpmsVoltage, 150);
    lv_obj_set_style_text_align(ui_LabelTpmsVoltage, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(ui_LabelTpmsVoltage, &ui_font_FontTypoderSize36, LV_PART_MAIN);
    lv_obj_set_style_text_color(ui_LabelTpmsVoltage, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_align(ui_LabelTpmsVoltage, LV_ALIGN_CENTER, 0, 20);

    lv_obj_t *ear = lv_img_create(ui_ScreenPageTpms);
    lv_img_set_src(ear, &ui_img_pngblackear_png);
    lv_obj_set_size(ear, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(ear, LV_ALIGN_CENTER, 0, -142);
    lv_obj_add_flag(ear, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(ear, LV_OBJ_FLAG_SCROLLABLE);

    ui_nav_attach_gesture(ui_ScreenPageTpms, UI_NAV_PAGE_TPMS);
}
