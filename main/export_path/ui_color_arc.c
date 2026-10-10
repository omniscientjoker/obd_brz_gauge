#include "ui_color_arc.h"

#include <string.h>

#define UI_COLOR_ARC_DEFAULT_SLICES 32

static uint16_t clamp_position(uint16_t position)
{
    return position > UI_COLOR_ARC_POSITION_MAX ? UI_COLOR_ARC_POSITION_MAX : position;
}

static lv_color_t color_at(const ui_color_arc_t *arc, uint16_t position)
{
    const ui_color_arc_stop_t *stops = arc->stops;
    uint8_t last = (uint8_t)(arc->stop_count - 1);
    if (position <= stops[0].position) return stops[0].color;
    for (uint8_t i = 1; i <= last; ++i) {
        if (position <= stops[i].position) {
            uint16_t span = stops[i].position - stops[i - 1].position;
            uint16_t remaining = stops[i].position - position;
            uint8_t mix = span ? (uint8_t)((uint32_t)remaining * 255U / span) : 0;
            return lv_color_mix(stops[i - 1].color, stops[i].color, mix);
        }
    }
    return stops[last].color;
}

static void update_slices(ui_color_arc_t *arc)
{
    if (!arc || arc->max_value <= arc->min_value) return;
    int64_t numerator = (int64_t)arc->value - arc->min_value;
    int64_t denominator = (int64_t)arc->max_value - arc->min_value;
    int32_t progress = (int32_t)(numerator * UI_COLOR_ARC_POSITION_MAX / denominator);
    if (progress < 0) progress = 0;
    if (progress > UI_COLOR_ARC_POSITION_MAX) progress = UI_COLOR_ARC_POSITION_MAX;

    for (uint8_t i = 0; i < arc->slice_count; ++i) {
        uint16_t start = (uint16_t)((uint32_t)i * UI_COLOR_ARC_POSITION_MAX / arc->slice_count);
        uint16_t end = (uint16_t)((uint32_t)(i + 1) * UI_COLOR_ARC_POSITION_MAX / arc->slice_count);
        uint16_t portion = progress > start ? (uint16_t)(progress - start) : 0;
        if (portion > end - start) portion = end - start;
        lv_arc_set_value(arc->slices[i], (int32_t)portion * UI_COLOR_ARC_POSITION_MAX / (end - start));
    }
}

bool ui_color_arc_create(ui_color_arc_t *arc, lv_obj_t *parent,
                         const ui_color_arc_config_t *config)
{
    if (!arc || !parent || !config || config->max_value <= config->min_value ||
        config->size <= 0 || config->width <= 0 || !config->stops ||
        config->stop_count < 2 || config->stop_count > UI_COLOR_ARC_MAX_STOPS)
        return false;

    uint8_t slice_count = config->slice_count ? config->slice_count : UI_COLOR_ARC_DEFAULT_SLICES;
    if (slice_count > UI_COLOR_ARC_MAX_SLICES) return false;

    memset(arc, 0, sizeof(*arc));
    arc->min_value = config->min_value;
    arc->max_value = config->max_value;
    arc->value = config->min_value;
    arc->stop_count = config->stop_count;
    arc->slice_count = slice_count;
    for (uint8_t i = 0; i < config->stop_count; ++i) {
        arc->stops[i] = config->stops[i];
        arc->stops[i].position = clamp_position(arc->stops[i].position);
        if (i > 0 && arc->stops[i].position < arc->stops[i - 1].position) return false;
    }
    for (uint8_t i = config->stop_count; i < UI_COLOR_ARC_MAX_STOPS; ++i)
        arc->stops[i] = arc->stops[config->stop_count - 1];

    arc->track = lv_arc_create(parent);
    lv_obj_set_size(arc->track, config->size, config->size);
    lv_obj_center(arc->track);
    lv_obj_clear_flag(arc->track, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(arc->track, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_arc_set_bg_angles(arc->track, 0, 360);
    lv_arc_set_rotation(arc->track, config->start_angle);
    lv_obj_set_style_arc_color(arc->track, config->track_color, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc->track, config->width, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc->track, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc->track, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(arc->track, LV_OPA_TRANSP, LV_PART_KNOB);

    for (uint8_t i = 0; i < slice_count; ++i) {
        lv_obj_t *slice = lv_arc_create(parent);
        arc->slices[i] = slice;
        lv_obj_set_size(slice, config->size, config->size);
        lv_obj_center(slice);
        lv_obj_clear_flag(slice, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(slice, LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_arc_set_range(slice, 0, UI_COLOR_ARC_POSITION_MAX);
        uint16_t segment_start = (uint16_t)((uint32_t)i * 360U / slice_count);
        uint16_t segment_end = (uint16_t)((uint32_t)(i + 1) * 360U / slice_count);
        lv_arc_set_bg_angles(slice, 0, segment_end - segment_start);
        lv_arc_set_rotation(slice, config->start_angle + segment_start);
        lv_arc_set_value(slice, 0);
        lv_obj_set_style_arc_opa(slice, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_arc_color(slice,
            color_at(arc, (uint16_t)(((uint32_t)i * UI_COLOR_ARC_POSITION_MAX +
                                       UI_COLOR_ARC_POSITION_MAX / 2) / slice_count)),
            LV_PART_INDICATOR);
        lv_obj_set_style_arc_width(slice, config->width, LV_PART_INDICATOR);
        lv_obj_set_style_arc_opa(slice, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(slice, false, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(slice, LV_OPA_TRANSP, LV_PART_KNOB);
    }
    return true;
}

bool ui_color_arc_set_range(ui_color_arc_t *arc, int32_t min_value,
                            int32_t max_value)
{
    if (!arc || !arc->track || max_value <= min_value) return false;
    arc->min_value = min_value;
    arc->max_value = max_value;
    update_slices(arc);
    return true;
}

void ui_color_arc_set_value(ui_color_arc_t *arc, int32_t value)
{
    if (!arc || !arc->track) return;
    arc->value = value;
    update_slices(arc);
}
