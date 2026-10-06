// Temperature page. The simulator presents one primary gauge with two
// secondary readings; the three slots remain configurable through NVS.

#include "../ui.h"

lv_obj_t *ui_LabelCoolantTempText = NULL;
lv_obj_t *ui_LabelOilTempText = NULL;
lv_obj_t *ui_LabelIntakeTempText = NULL;
lv_obj_t *ui_LabelTempValue[3] = {NULL, NULL, NULL};
lv_obj_t *ui_LabelTempName[3] = {NULL, NULL, NULL};
lv_obj_t *ui_LabelTempUnit[3] = {NULL, NULL, NULL};
lv_obj_t *ui_LabelTempDot[3] = {NULL, NULL, NULL};
lv_obj_t *ui_TempArc = NULL;

static lv_obj_t *create_color_dot(lv_obj_t *parent, lv_coord_t x, lv_coord_t y)
{
    lv_obj_t *dot = lv_obj_create(parent);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 7, 7);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(dot, lv_color_hex(0x44AAFF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_align(dot, LV_ALIGN_CENTER, x, y);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return dot;
}

static void create_temp_slot(lv_obj_t *parent, uint8_t index, lv_coord_t x,
                             lv_coord_t name_y, lv_coord_t value_y,
                             lv_coord_t unit_y, const char *name,
                             const char *unit)
{
    if (index > 0) {
        // Secondary readings are a single compact bottom row in the
        // simulator. Keep separate labels for the data updater, but place
        // them side by side rather than stacking three rows vertically.
        ui_LabelTempName[index] = lv_label_create(parent);
        lv_label_set_text(ui_LabelTempName[index], name);
        lv_obj_set_width(ui_LabelTempName[index], 54);
        lv_obj_set_style_text_align(ui_LabelTempName[index], LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
        lv_obj_set_style_text_font(ui_LabelTempName[index], &ui_font_FontTypoderSize16, LV_PART_MAIN);
        lv_obj_set_style_text_color(ui_LabelTempName[index], lv_color_hex(0x777777), LV_PART_MAIN);
        lv_obj_align(ui_LabelTempName[index], LV_ALIGN_CENTER, x - 38, 126);

        ui_LabelTempValue[index] = lv_label_create(parent);
        lv_label_set_text(ui_LabelTempValue[index], "--");
        lv_obj_set_width(ui_LabelTempValue[index], 42);
        lv_obj_set_style_text_align(ui_LabelTempValue[index], LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_font(ui_LabelTempValue[index], &ui_font_FontTypoderSize16, LV_PART_MAIN);
        lv_obj_set_style_text_color(ui_LabelTempValue[index], lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_align(ui_LabelTempValue[index], LV_ALIGN_CENTER, x + 2, 126);

        ui_LabelTempUnit[index] = lv_label_create(parent);
        lv_label_set_text(ui_LabelTempUnit[index], unit);
        lv_obj_set_width(ui_LabelTempUnit[index], 28);
        lv_obj_set_style_text_align(ui_LabelTempUnit[index], LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        lv_obj_set_style_text_font(ui_LabelTempUnit[index], &ui_font_FontTypoderSize16, LV_PART_MAIN);
        lv_obj_set_style_text_color(ui_LabelTempUnit[index], lv_color_hex(0x777777), LV_PART_MAIN);
        lv_obj_align(ui_LabelTempUnit[index], LV_ALIGN_CENTER, x + 33, 126);
        ui_LabelTempDot[index] = create_color_dot(parent, x - 61, 126);
        lv_obj_add_flag(ui_LabelTempDot[index], LV_OBJ_FLAG_HIDDEN);
        return;
    }

    ui_LabelTempName[index] = lv_label_create(parent);
    lv_label_set_text(ui_LabelTempName[index], name);
    lv_obj_set_style_text_font(ui_LabelTempName[index],
                               index == 0 ? &ui_font_FontTypoderSize20 : &ui_font_FontTypoderSize16,
                               LV_PART_MAIN);
    lv_obj_set_style_text_color(ui_LabelTempName[index], lv_color_hex(0x44AAFF), LV_PART_MAIN);
    lv_obj_set_width(ui_LabelTempName[index], index == 0 ? 170 : 120);
    lv_obj_set_style_text_align(ui_LabelTempName[index], LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(ui_LabelTempName[index], LV_ALIGN_CENTER, x, name_y);

    ui_LabelTempValue[index] = lv_label_create(parent);
    lv_label_set_text(ui_LabelTempValue[index], "--");
    lv_label_set_long_mode(ui_LabelTempValue[index], LV_LABEL_LONG_CLIP);
    lv_obj_set_width(ui_LabelTempValue[index], index == 0 ? 190 : 120);
    lv_obj_set_style_text_align(ui_LabelTempValue[index], LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(ui_LabelTempValue[index],
                               index == 0 ? &ui_font_FontTypoderSize56 : &ui_font_FontTypoderSize20,
                               LV_PART_MAIN);
    lv_obj_set_style_text_color(ui_LabelTempValue[index], lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_align(ui_LabelTempValue[index], LV_ALIGN_CENTER, x, value_y);

    ui_LabelTempUnit[index] = lv_label_create(parent);
    lv_label_set_text(ui_LabelTempUnit[index], unit);
    lv_obj_set_width(ui_LabelTempUnit[index], index == 0 ? 170 : 120);
    lv_obj_set_style_text_align(ui_LabelTempUnit[index], LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(ui_LabelTempUnit[index], &ui_font_FontTypoderSize16, LV_PART_MAIN);
    lv_obj_set_style_text_color(ui_LabelTempUnit[index], lv_color_hex(0x777777), LV_PART_MAIN);
    lv_obj_align(ui_LabelTempUnit[index], LV_ALIGN_CENTER, x, unit_y);

    ui_LabelTempDot[index] = create_color_dot(parent, x - (index == 0 ? 0 : 54), name_y);
}

void ui_ScreenPageTemp_screen_init(void)
{
    ui_ScreenPageTemp = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_ScreenPageTemp, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(ui_ScreenPageTemp, 360, LV_PART_MAIN);
    ui_helpers_style_screen_bg(ui_ScreenPageTemp);
    lv_obj_set_style_bg_opa(ui_ScreenPageTemp, 255, LV_PART_MAIN);
    lv_obj_t *ring = ui_helpers_create_ring(ui_ScreenPageTemp, 10);

    ui_TempArc = lv_arc_create(ui_ScreenPageTemp);
    // Match the simulator's 230px primary gauge ring on the 360px display.
    lv_obj_set_size(ui_TempArc, 230, 230);
    lv_obj_center(ui_TempArc);
    lv_arc_set_range(ui_TempArc, 0, 100);
    lv_arc_set_bg_angles(ui_TempArc, 0, 360);
    lv_arc_set_value(ui_TempArc, 0);
    lv_obj_clear_flag(ui_TempArc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(ui_TempArc, 11, LV_PART_MAIN);
    lv_obj_set_style_arc_color(ui_TempArc, ui_theme_color_lv(UI_COLOR_ARC_TRACK), LV_PART_MAIN);
    lv_obj_set_style_arc_width(ui_TempArc, 11, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(ui_TempArc, ui_theme_color_lv(UI_COLOR_ARC_INDICATOR), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(ui_TempArc, false, LV_PART_MAIN | LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(ui_TempArc, LV_OPA_COVER, LV_PART_MAIN | LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(ui_TempArc, 0, LV_PART_KNOB);

    create_temp_slot(ui_ScreenPageTemp, 0, 0, -35, 0, 38, "CLT", "'C");
    create_temp_slot(ui_ScreenPageTemp, 1, -72, 101, 119, 141, "OIL", "'C");
    create_temp_slot(ui_ScreenPageTemp, 2, 72, 101, 119, 141, "IAT", "'C");

    ui_LabelCoolantTempText = ui_LabelTempValue[0];
    ui_LabelOilTempText = ui_LabelTempValue[1];
    ui_LabelIntakeTempText = ui_LabelTempValue[2];

    lv_obj_t *ear = lv_img_create(ui_ScreenPageTemp);
    lv_img_set_src(ear, &ui_img_pngblackear_png);
    lv_obj_set_size(ear, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(ear, LV_ALIGN_CENTER, 0, -142);
    lv_obj_add_flag(ear, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(ear, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_move_foreground(ring);
    ui_nav_attach_gesture(ui_ScreenPageTemp, UI_NAV_PAGE_TEMP);
}
