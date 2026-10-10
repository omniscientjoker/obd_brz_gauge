#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UI_COLOR_ARC_POSITION_MAX 1000
#define UI_COLOR_ARC_MAX_STOPS 8
#define UI_COLOR_ARC_MAX_SLICES 48

typedef struct {
    uint16_t position;  // 0..UI_COLOR_ARC_POSITION_MAX
    lv_color_t color;
} ui_color_arc_stop_t;

typedef struct {
    int32_t min_value;
    int32_t max_value;
    lv_coord_t size;
    lv_coord_t width;
    uint16_t start_angle;  // degrees; 90 starts at 6 o'clock in LVGL
    lv_color_t track_color;
    const ui_color_arc_stop_t *stops;
    uint8_t stop_count;
    uint8_t slice_count;   // 0 selects the default resolution
} ui_color_arc_config_t;

typedef struct {
    lv_obj_t *track;
    lv_obj_t *slices[UI_COLOR_ARC_MAX_SLICES];
    ui_color_arc_stop_t stops[UI_COLOR_ARC_MAX_STOPS];
    int32_t min_value;
    int32_t max_value;
    int32_t value;
    uint8_t stop_count;
    uint8_t slice_count;
} ui_color_arc_t;

bool ui_color_arc_create(ui_color_arc_t *arc, lv_obj_t *parent,
                         const ui_color_arc_config_t *config);
bool ui_color_arc_set_range(ui_color_arc_t *arc, int32_t min_value,
                            int32_t max_value);
void ui_color_arc_set_value(ui_color_arc_t *arc, int32_t value);

#ifdef __cplusplus
}
#endif
