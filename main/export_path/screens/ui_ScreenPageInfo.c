// Compact information tiles with five configurable data sources.

#include "../ui.h"

lv_obj_t *ui_ScreenPageInfo = NULL;
lv_obj_t *ui_LabelInfoCLT = NULL;
lv_obj_t *ui_LabelInfoIAT = NULL;
lv_obj_t *ui_LabelInfoLoad = NULL;
lv_obj_t *ui_LabelInfoTPS = NULL;
lv_obj_t *ui_LabelInfoOil = NULL;
lv_obj_t *ui_LabelInfoValue[5] = {NULL, NULL, NULL, NULL, NULL};
lv_obj_t *ui_LabelInfoName[5] = {NULL, NULL, NULL, NULL, NULL};
lv_obj_t *ui_LabelInfoUnit[5] = {NULL, NULL, NULL, NULL, NULL};
lv_obj_t *ui_LabelInfoFuel = NULL;
lv_obj_t *ui_LabelInfoFuelNeed = NULL;

static lv_obj_t *create_info_tile(lv_obj_t *parent, uint8_t index,
                                  const char *name, const char *unit,
                                  lv_coord_t cx, lv_coord_t cy,
                                  lv_coord_t width)
{
    lv_obj_t *tile = lv_obj_create(parent);
    lv_obj_remove_style_all(tile);
    lv_obj_set_size(tile, width, 64);
    lv_obj_set_style_radius(tile, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x101819), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tile, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_border_width(tile, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(tile, lv_color_hex(0x344143), LV_PART_MAIN);
    lv_obj_align(tile, LV_ALIGN_CENTER, cx, cy);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    ui_LabelInfoName[index] = lv_label_create(tile);
    lv_label_set_text(ui_LabelInfoName[index], name);
    lv_obj_set_width(ui_LabelInfoName[index], width - 12);
    lv_obj_set_style_text_align(ui_LabelInfoName[index], LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(ui_LabelInfoName[index], &ui_font_FontTypoderSize16, LV_PART_MAIN);
    lv_obj_set_style_text_color(ui_LabelInfoName[index], lv_color_hex(0x42D1D4), LV_PART_MAIN);
    lv_obj_align(ui_LabelInfoName[index], LV_ALIGN_CENTER, 0, -14);

    ui_LabelInfoValue[index] = lv_label_create(tile);
    lv_label_set_text(ui_LabelInfoValue[index], "--");
    lv_obj_set_width(ui_LabelInfoValue[index], width > 140 ? 92 : 70);
    lv_obj_set_style_text_align(ui_LabelInfoValue[index], LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(ui_LabelInfoValue[index], &ui_font_FontTypoderSize24, LV_PART_MAIN);
    lv_obj_set_style_text_color(ui_LabelInfoValue[index], lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_align(ui_LabelInfoValue[index], LV_ALIGN_CENTER, -19, 11);

    ui_LabelInfoUnit[index] = lv_label_create(tile);
    lv_label_set_text(ui_LabelInfoUnit[index], unit);
    lv_obj_set_width(ui_LabelInfoUnit[index], 44);
    lv_obj_set_style_text_align(ui_LabelInfoUnit[index], LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(ui_LabelInfoUnit[index], &ui_font_FontTypoderSize16, LV_PART_MAIN);
    lv_obj_set_style_text_color(ui_LabelInfoUnit[index], lv_color_hex(0x91A1A1), LV_PART_MAIN);
    lv_obj_align(ui_LabelInfoUnit[index], LV_ALIGN_CENTER, width > 140 ? 49 : 42, 12);

    return ui_LabelInfoValue[index];
}

void ui_ScreenPageInfo_screen_init(void)
{
    ui_ScreenPageInfo = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_ScreenPageInfo, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(ui_ScreenPageInfo, 360, LV_PART_MAIN);
    ui_helpers_style_screen_bg(ui_ScreenPageInfo);
    lv_obj_set_style_bg_opa(ui_ScreenPageInfo, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(ui_ScreenPageInfo);
    lv_label_set_text(title, "INFO");
    lv_obj_set_style_text_font(title, &ui_font_FontTypoderSize20, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(0x91A1A1), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -126);
    // INFO is already represented by the simulator-compatible status row.
    lv_obj_add_flag(title, LV_OBJ_FLAG_HIDDEN);

    ui_LabelInfoFuel = lv_label_create(ui_ScreenPageInfo);
    lv_label_set_text(ui_LabelInfoFuel, "油量 --% 约 --.-L");
    lv_obj_set_style_text_font(ui_LabelInfoFuel, &ui_font_Chinese16, LV_PART_MAIN);
    lv_obj_set_style_text_color(ui_LabelInfoFuel, lv_color_hex(0x91A1A1), LV_PART_MAIN);
    lv_obj_align(ui_LabelInfoFuel, LV_ALIGN_CENTER, -80, -112);
    ui_LabelInfoFuelNeed = lv_label_create(ui_ScreenPageInfo);
    lv_label_set_text(ui_LabelInfoFuelNeed, "加满 --% 约 --.-L");
    lv_obj_set_style_text_font(ui_LabelInfoFuelNeed, &ui_font_Chinese16, LV_PART_MAIN);
    lv_obj_set_style_text_color(ui_LabelInfoFuelNeed, lv_color_hex(0x91A1A1), LV_PART_MAIN);
    lv_obj_align(ui_LabelInfoFuelNeed, LV_ALIGN_CENTER, 80, -112);

    // Four primary tiles mirror the simulator grid, with the fifth configured
    // slot centered below them inside the circular display safe area.
    ui_LabelInfoCLT = create_info_tile(ui_ScreenPageInfo, 0, "RPM", "rpm", -70, -18, 134);
    ui_LabelInfoOil = create_info_tile(ui_ScreenPageInfo, 1, "SPEED", "km/h",  70, -18, 134);
    ui_LabelInfoLoad = create_info_tile(ui_ScreenPageInfo, 2, "CLT", "'C", -70,  53, 134);
    ui_LabelInfoTPS = create_info_tile(ui_ScreenPageInfo, 3, "BAT", "V",  70,  53, 134);

    lv_obj_t *ear = lv_img_create(ui_ScreenPageInfo);
    lv_img_set_src(ear, &ui_img_pngblackear_png);
    lv_obj_set_size(ear, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(ear, LV_ALIGN_CENTER, 0, -142);
    lv_obj_add_flag(ear, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(ear, LV_OBJ_FLAG_SCROLLABLE);

    ui_nav_attach_gesture(ui_ScreenPageInfo, UI_NAV_PAGE_INFO);
}
