// TPMS and ECU voltage page. Tire pressures remain unavailable until a
// vehicle-specific TPMS reader stores real values in obd_data_cache.

#include "../ui.h"

lv_obj_t *ui_ScreenPageTpms = NULL;
lv_obj_t *ui_LabelTpmsValue[4] = {NULL, NULL, NULL, NULL};
lv_obj_t *ui_LabelTpmsVoltage = NULL;

static void create_tire_tile(lv_obj_t *parent, int index, const char *name, lv_coord_t x, lv_coord_t y)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, name);
    lv_obj_set_style_text_font(label, &ui_font_FontTypoderSize16, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(0x5CAEFF), LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_CENTER, x, y - 22);

    ui_LabelTpmsValue[index] = lv_label_create(parent);
    lv_label_set_text(ui_LabelTpmsValue[index], "--.-");
    lv_obj_set_width(ui_LabelTpmsValue[index], 120);
    lv_obj_set_style_text_align(ui_LabelTpmsValue[index], LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(ui_LabelTpmsValue[index], &ui_font_FontTypoderSize36, LV_PART_MAIN);
    lv_obj_set_style_text_color(ui_LabelTpmsValue[index], lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_align(ui_LabelTpmsValue[index], LV_ALIGN_CENTER, x, y + 4);

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

    lv_obj_t *ring = ui_helpers_create_ring(ui_ScreenPageTpms, 8);

    lv_obj_t *title = lv_label_create(ui_ScreenPageTpms);
    lv_label_set_text(title, "TPMS");
    lv_obj_set_style_text_font(title, &ui_font_FontTypoderSize20, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(0x666666), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -148);

    create_tire_tile(ui_ScreenPageTpms, 0, "FL", -92, -79);
    create_tire_tile(ui_ScreenPageTpms, 1, "FR",  92, -79);
    create_tire_tile(ui_ScreenPageTpms, 2, "RL", -92,  81);
    create_tire_tile(ui_ScreenPageTpms, 3, "RR",  92,  81);

    lv_obj_t *voltage_name = lv_label_create(ui_ScreenPageTpms);
    lv_label_set_text(voltage_name, "VOLTAGE");
    lv_obj_set_style_text_font(voltage_name, &ui_font_FontTypoderSize16, LV_PART_MAIN);
    lv_obj_set_style_text_color(voltage_name, lv_color_hex(0xE0B85A), LV_PART_MAIN);
    lv_obj_align(voltage_name, LV_ALIGN_CENTER, 0, -8);

    ui_LabelTpmsVoltage = lv_label_create(ui_ScreenPageTpms);
    lv_label_set_text(ui_LabelTpmsVoltage, "--.- V");
    lv_obj_set_width(ui_LabelTpmsVoltage, 150);
    lv_obj_set_style_text_align(ui_LabelTpmsVoltage, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(ui_LabelTpmsVoltage, &ui_font_FontTypoderSize36, LV_PART_MAIN);
    lv_obj_set_style_text_color(ui_LabelTpmsVoltage, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_align(ui_LabelTpmsVoltage, LV_ALIGN_CENTER, 0, 22);

    lv_obj_t *ear = lv_img_create(ui_ScreenPageTpms);
    lv_img_set_src(ear, &ui_img_pngblackear_png);
    lv_obj_set_size(ear, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(ear, LV_ALIGN_CENTER, 0, -142);
    lv_obj_add_flag(ear, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(ear, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_move_foreground(ring);
    lv_obj_add_event_cb(ui_ScreenPageTpms, ui_event_tpms_background, LV_EVENT_GESTURE, NULL);
}
