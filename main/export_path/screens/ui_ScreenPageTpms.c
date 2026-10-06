#include "../ui.h"

lv_obj_t *ui_LabelTpmsValue[4] = {NULL, NULL, NULL, NULL};
lv_obj_t *ui_LabelTpmsName[4] = {NULL, NULL, NULL, NULL};

static const char *const s_wheel_names[4] = {"FL", "FR", "RL", "RR"};
static const lv_coord_t s_x[4] = {-82, 82, -82, 82};
static const lv_coord_t s_y[4] = {-72, -72, 42, 42};

static lv_obj_t *create_value_label(lv_obj_t *parent, uint8_t index)
{
    lv_obj_t *name = lv_label_create(parent);
    lv_label_set_text(name, s_wheel_names[index]);
    lv_obj_set_style_text_font(name, &ui_font_FontTypoderSize20, LV_PART_MAIN);
    lv_obj_set_style_text_color(name, lv_color_hex(0x66CCFF), LV_PART_MAIN);
    lv_obj_align(name, LV_ALIGN_CENTER, s_x[index], s_y[index] - 27);
    ui_LabelTpmsName[index] = name;

    lv_obj_t *value = lv_label_create(parent);
    lv_label_set_text(value, "--");
    lv_obj_set_style_text_font(value, &ui_font_FontTypoderSize40, LV_PART_MAIN);
    lv_obj_set_style_text_color(value, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_width(value, 130);
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(value, LV_ALIGN_CENTER, s_x[index], s_y[index] + 7);
    ui_LabelTpmsValue[index] = value;
    return value;
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
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -145);

    for (uint8_t i = 0; i < 4; ++i) create_value_label(ui_ScreenPageTpms, i);

    lv_obj_t *unit = lv_label_create(ui_ScreenPageTpms);
    lv_label_set_text(unit, "bar");
    lv_obj_set_style_text_font(unit, &ui_font_FontTypoderSize16, LV_PART_MAIN);
    lv_obj_set_style_text_color(unit, lv_color_hex(0x888888), LV_PART_MAIN);
    lv_obj_align(unit, LV_ALIGN_CENTER, 0, 132);

    lv_obj_t *hint = lv_label_create(ui_ScreenPageTpms);
    lv_label_set_text(hint, "-- = no fresh response");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x666666), LV_PART_MAIN);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 155);

    lv_obj_t *ear = lv_img_create(ui_ScreenPageTpms);
    lv_img_set_src(ear, &ui_img_pngblackear_png);
    lv_obj_align(ear, LV_ALIGN_CENTER, 0, -142);

    lv_obj_move_foreground(ring);
    lv_obj_add_event_cb(ui_ScreenPageTpms, ui_event_tpms_background,
                        LV_EVENT_GESTURE, NULL);
}
