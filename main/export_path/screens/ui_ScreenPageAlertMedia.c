#include "../ui.h"
#include "../ui_navigation.h"

#include <stdio.h>
#include <strings.h>
#include <string.h>

#include "app_media/sd_media_manager.h"
#include "app_media/wav_player.h"
#include "bsp_obd_dsp/nvs_storage.h"
#include "export_path/ui_ext.h"

static const char *const s_alert_names[NVS_MEDIA_ALERT_COUNT] = {
    "胎压告警", "电压告警", "速度告警", "水温告警", "低油量告警", "转速告警",
};

static lv_obj_t *s_value_labels[NVS_MEDIA_ALERT_COUNT];
static lv_obj_t *s_status_label;
static lv_obj_t *s_picker_overlay;
static lv_obj_t *s_picker_list;
static lv_obj_t *s_mode_buttons[NVS_MEDIA_ALERT_VIDEO + 1];
static lv_timer_t *s_refresh_timer;
static uint8_t s_selected_alert;
static uint8_t s_selected_mode;
static char s_picker_names[SD_MEDIA_RESOURCE_MAX][SD_MEDIA_RESOURCE_NAME_MAX];
static sd_media_resource_snapshot_t s_picker_snapshot;
static bool s_picker_snapshot_valid;
static bool s_picker_scan_pending;

static const char *mode_name(uint8_t mode)
{
    return mode == NVS_MEDIA_ALERT_VIDEO ? "视频" : "音频";
}

static void refresh_values(void)
{
    nvs_media_alert_cfg_t cfg = {0};
    if (nvs_media_alert_cfg_get(&cfg) != ESP_OK) return;
    for (uint8_t i = 0; i < NVS_MEDIA_ALERT_COUNT; ++i) {
        if (!s_value_labels[i]) continue;
        if (cfg.resource[i][0]) {
            lv_label_set_text_fmt(s_value_labels[i], "%s %s", mode_name(cfg.mode[i]), cfg.resource[i]);
        } else {
            lv_label_set_text(s_value_labels[i], "未设置");
        }
    }
}

static void refresh_status(void)
{
    if (!s_status_label) return;
    sd_media_resource_snapshot_t snapshot;
    sd_media_get_resource_snapshot(&snapshot);
    if (snapshot.state == SD_MEDIA_STATE_NO_CARD) {
        lv_label_set_text(s_status_label, "未检测到 SD 卡");
    } else if (snapshot.state == SD_MEDIA_STATE_MOUNTING || snapshot.indexing) {
        lv_label_set_text(s_status_label, "正在读取媒体资源");
    } else if (snapshot.state == SD_MEDIA_STATE_ERROR) {
        lv_label_set_text(s_status_label, "SD 卡不可用");
    } else if (snapshot.audio_count == 0 && snapshot.video_count == 0) {
        lv_label_set_text(s_status_label, "无媒体文件");
    } else {
        lv_label_set_text_fmt(s_status_label, "音频 %u  视频 %u",
                              snapshot.audio_count, snapshot.video_count);
    }
}

static void close_picker(void)
{
    wav_player_stop();
    if (s_picker_overlay) {
        lv_obj_del(s_picker_overlay);
        s_picker_overlay = NULL;
        s_picker_list = NULL;
    }
    memset(s_mode_buttons, 0, sizeof(s_mode_buttons));
    s_picker_snapshot_valid = false;
    s_picker_scan_pending = false;
}

static void on_picker_close(lv_event_t *event)
{
    LV_UNUSED(event);
    close_picker();
}

static void on_picker_backdrop(lv_event_t *event)
{
    if (lv_event_get_target(event) == s_picker_overlay) close_picker();
}

static void apply_resource(const char *name)
{
    nvs_media_alert_cfg_t cfg = {0};
    if (s_selected_alert >= NVS_MEDIA_ALERT_COUNT || nvs_media_alert_cfg_get(&cfg) != ESP_OK) return;
    cfg.mode[s_selected_alert] = s_selected_mode;
    strncpy(cfg.resource[s_selected_alert], name ? name : "", NVS_MEDIA_RESOURCE_NAME_MAX - 1);
    cfg.resource[s_selected_alert][NVS_MEDIA_RESOURCE_NAME_MAX - 1] = '\0';
    if (nvs_media_alert_cfg_set(&cfg) == ESP_OK) refresh_values();
}

static bool resource_path(char *path, size_t path_len, const char *name, const char *extension)
{
    if (!path || !name || !extension) return false;
    const char *dot = strrchr(name, '.');
    if (!dot || strlen(dot) != 4 || strcasecmp(dot, extension) != 0) return false;
    return snprintf(path, path_len, "/sdcard/ALERT/%.*s%s", (int)(dot - name), name,
                    extension) > 0;
}

static void on_audio_resource_selected(lv_event_t *event)
{
    uint8_t index = (uint8_t)(uintptr_t)lv_event_get_user_data(event);
    if (index >= SD_MEDIA_RESOURCE_MAX || s_picker_names[index][0] == '\0') return;
    apply_resource(s_picker_names[index]);
    char path[96] = {0};
    if (resource_path(path, sizeof(path), s_picker_names[index], ".WAV")) {
        (void)wav_player_preview(path);
    }
}

static void on_video_resource_preview(lv_event_t *event)
{
    uint8_t index = (uint8_t)(uintptr_t)lv_event_get_user_data(event);
    if (index >= SD_MEDIA_RESOURCE_MAX || s_picker_names[index][0] == '\0') return;
    apply_resource(s_picker_names[index]);
    wav_player_stop();

    char manifest_path[96] = {0};
    char data_path[96] = {0};
    if (resource_path(manifest_path, sizeof(manifest_path), s_picker_names[index], ".TXT") &&
        resource_path(data_path, sizeof(data_path), s_picker_names[index], ".BIN")) {
        (void)ui_ext_preview_alert_video(manifest_path, data_path);
    }
}

static void on_resource_clear(lv_event_t *event)
{
    LV_UNUSED(event);
    wav_player_stop();
    apply_resource(NULL);
    close_picker();
}

static void style_list_button(lv_obj_t *button)
{
    lv_obj_set_height(button, 30);
    lv_obj_set_style_bg_color(button, ui_theme_color_lv(UI_COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(button, 4, LV_PART_MAIN);
    lv_obj_set_style_text_font(button, &ui_font_Chinese16, LV_PART_MAIN);
    lv_obj_set_style_text_color(button, ui_theme_color_lv(UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
}

static void style_mode_buttons(void)
{
    for (uint8_t mode = NVS_MEDIA_ALERT_AUDIO; mode <= NVS_MEDIA_ALERT_VIDEO; ++mode) {
        if (!s_mode_buttons[mode]) continue;
        lv_obj_set_style_bg_color(s_mode_buttons[mode],
                                  mode == s_selected_mode ? lv_color_hex(0x009B58) :
                                  ui_theme_color_lv(UI_COLOR_ARC_TRACK), LV_PART_MAIN);
    }
}

static bool picker_snapshot_equals(const sd_media_resource_snapshot_t *a,
                                   const sd_media_resource_snapshot_t *b)
{
    if (!a || !b || a->state != b->state || a->indexing != b->indexing ||
        a->audio_count != b->audio_count || a->video_count != b->video_count) {
        return false;
    }
    return memcmp(a->audio, b->audio, sizeof(a->audio)) == 0 &&
           memcmp(a->video, b->video, sizeof(a->video)) == 0;
}

static void refresh_picker_list(void)
{
    if (!s_picker_list) return;
    sd_media_resource_snapshot_t snapshot;
    sd_media_get_resource_snapshot(&snapshot);
    if (s_picker_snapshot_valid && picker_snapshot_equals(&snapshot, &s_picker_snapshot)) {
        return;
    }
    lv_obj_clean(s_picker_list);
    memset(s_picker_names, 0, sizeof(s_picker_names));
    s_picker_snapshot = snapshot;
    s_picker_snapshot_valid = true;

    if (snapshot.state == SD_MEDIA_STATE_NO_CARD) {
        lv_obj_t *label = lv_label_create(s_picker_list);
        lv_label_set_text(label, "未检测到 SD 卡");
        lv_obj_set_style_text_font(label, &ui_font_Chinese16, LV_PART_MAIN);
        lv_obj_center(label);
        return;
    }
    if (snapshot.state != SD_MEDIA_STATE_READY || snapshot.indexing) {
        lv_obj_t *label = lv_label_create(s_picker_list);
        lv_label_set_text(label, "正在读取媒体资源");
        lv_obj_set_style_text_font(label, &ui_font_Chinese16, LV_PART_MAIN);
        lv_obj_center(label);
        return;
    }

    const uint8_t count = s_selected_mode == NVS_MEDIA_ALERT_VIDEO ?
                          snapshot.video_count : snapshot.audio_count;
    char (*items)[SD_MEDIA_RESOURCE_NAME_MAX] = s_selected_mode == NVS_MEDIA_ALERT_VIDEO ?
        snapshot.video : snapshot.audio;
    if (count == 0) {
        lv_obj_t *label = lv_label_create(s_picker_list);
        lv_label_set_text(label, "无媒体文件");
        lv_obj_set_style_text_font(label, &ui_font_Chinese16, LV_PART_MAIN);
        lv_obj_center(label);
        return;
    }
    for (uint8_t i = 0; i < count; ++i) {
        strncpy(s_picker_names[i], items[i], SD_MEDIA_RESOURCE_NAME_MAX - 1);
        lv_obj_t *button = lv_list_add_btn(s_picker_list, NULL, s_picker_names[i]);
        style_list_button(button);
        if (s_selected_mode == NVS_MEDIA_ALERT_AUDIO) {
            lv_obj_add_event_cb(button, on_audio_resource_selected, LV_EVENT_CLICKED,
                                (void *)(uintptr_t)i);
        } else {
            lv_obj_add_event_cb(button, on_video_resource_preview, LV_EVENT_LONG_PRESSED,
                                (void *)(uintptr_t)i);
        }
    }
    lv_obj_t *clear = lv_list_add_btn(s_picker_list, NULL, "清除设置");
    style_list_button(clear);
    lv_obj_add_event_cb(clear, on_resource_clear, LV_EVENT_CLICKED, NULL);
}

static void on_mode_selected(lv_event_t *event)
{
    wav_player_stop();
    s_selected_mode = (uint8_t)(uintptr_t)lv_event_get_user_data(event);
    s_picker_snapshot_valid = false;
    style_mode_buttons();
    refresh_picker_list();
}

static void open_picker(uint8_t alert)
{
    if (alert >= NVS_MEDIA_ALERT_COUNT) return;
    close_picker();
    s_selected_alert = alert;
    s_picker_snapshot_valid = false;
    s_picker_scan_pending = true;
    sd_media_request_resource_scan();
    nvs_media_alert_cfg_t cfg = {0};
    if (nvs_media_alert_cfg_get(&cfg) == ESP_OK) s_selected_mode = cfg.mode[alert];

    s_picker_overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_picker_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_picker_overlay, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_picker_overlay, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_picker_overlay, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_picker_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_picker_overlay, on_picker_backdrop, LV_EVENT_CLICKED, NULL);

    lv_obj_t *panel = lv_obj_create(s_picker_overlay);
    lv_obj_set_size(panel, 270, 244);
    lv_obj_center(panel);
    lv_obj_set_style_bg_color(panel, ui_theme_color_lv(UI_COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(panel, ui_theme_color_lv(UI_COLOR_TEXT_SECONDARY), LV_PART_MAIN);
    lv_obj_set_style_radius(panel, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, 8, LV_PART_MAIN);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(panel);
    lv_label_set_text(title, s_alert_names[alert]);
    lv_obj_set_style_text_font(title, &ui_font_Chinese20, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, ui_theme_color_lv(UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 2, 0);

    lv_obj_t *close = lv_btn_create(panel);
    lv_obj_set_size(close, 24, 24);
    lv_obj_align(close, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(close, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_radius(close, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(close, 0, LV_PART_MAIN);
    lv_obj_t *close_label = lv_label_create(close);
    lv_label_set_text(close_label, LV_SYMBOL_CLOSE);
    lv_obj_center(close_label);
    lv_obj_add_event_cb(close, on_picker_close, LV_EVENT_CLICKED, NULL);

    for (uint8_t mode = NVS_MEDIA_ALERT_AUDIO; mode <= NVS_MEDIA_ALERT_VIDEO; ++mode) {
        lv_obj_t *button = lv_btn_create(panel);
        lv_obj_set_size(button, 120, 28);
        lv_obj_align(button, LV_ALIGN_TOP_LEFT, mode == NVS_MEDIA_ALERT_AUDIO ? 2 : 128, 32);
        s_mode_buttons[mode] = button;
        lv_obj_set_style_bg_color(button, ui_theme_color_lv(UI_COLOR_ARC_TRACK), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(button, 4, LV_PART_MAIN);
        lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
        lv_obj_t *label = lv_label_create(button);
        lv_label_set_text(label, mode_name(mode));
        lv_obj_set_style_text_font(label, &ui_font_Chinese16, LV_PART_MAIN);
        lv_obj_center(label);
        lv_obj_add_event_cb(button, on_mode_selected, LV_EVENT_CLICKED, (void *)(uintptr_t)mode);
    }
    style_mode_buttons();

    s_picker_list = lv_list_create(panel);
    lv_obj_set_size(s_picker_list, 252, 166);
    lv_obj_align(s_picker_list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(s_picker_list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_picker_list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_picker_list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_picker_list, 4, LV_PART_MAIN);
    refresh_picker_list();
}

static void on_alert_clicked(lv_event_t *event)
{
    open_picker((uint8_t)(uintptr_t)lv_event_get_user_data(event));
}

static void on_screen_loaded(lv_event_t *event)
{
    LV_UNUSED(event);
    refresh_status();
    refresh_values();
}

static void on_refresh_timer(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    refresh_status();
    if (s_picker_overlay && s_picker_scan_pending) {
        sd_media_resource_snapshot_t snapshot;
        sd_media_get_resource_snapshot(&snapshot);
        if (!snapshot.indexing &&
            (!s_picker_snapshot_valid ||
             !picker_snapshot_equals(&snapshot, &s_picker_snapshot))) {
            refresh_picker_list();
            s_picker_scan_pending = false;
        } else if (!snapshot.indexing && s_picker_snapshot_valid) {
            s_picker_scan_pending = false;
        }
    }
}

static void on_screen_delete(lv_event_t *event)
{
    LV_UNUSED(event);
    if (s_refresh_timer) {
        lv_timer_del(s_refresh_timer);
        s_refresh_timer = NULL;
    }
    close_picker();
    for (uint8_t i = 0; i < NVS_MEDIA_ALERT_COUNT; ++i) s_value_labels[i] = NULL;
    s_status_label = NULL;
    ui_ScreenPageAlertMedia = NULL;
}

void ui_ScreenPageAlertMedia_screen_init(void)
{
    ui_ScreenPageAlertMedia = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_ScreenPageAlertMedia, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(ui_ScreenPageAlertMedia, 360, LV_PART_MAIN);
    ui_helpers_style_screen_bg(ui_ScreenPageAlertMedia);
    lv_obj_set_style_bg_opa(ui_ScreenPageAlertMedia, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(ui_ScreenPageAlertMedia, 0, LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(ui_ScreenPageAlertMedia);
    lv_label_set_text(title, "告警媒体");
    lv_obj_set_style_text_font(title, &ui_font_Chinese20, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, ui_theme_color_lv(UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -142);

    s_status_label = lv_label_create(ui_ScreenPageAlertMedia);
    lv_obj_set_width(s_status_label, 250);
    lv_obj_set_style_text_align(s_status_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_status_label, &ui_font_Chinese16, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_status_label, ui_theme_color_lv(UI_COLOR_TEXT_SECONDARY), LV_PART_MAIN);
    lv_obj_align(s_status_label, LV_ALIGN_CENTER, 0, -118);

    for (uint8_t i = 0; i < NVS_MEDIA_ALERT_COUNT; ++i) {
        int32_t y = -82 + (int32_t)i * 31;
        lv_obj_t *name = lv_label_create(ui_ScreenPageAlertMedia);
        lv_label_set_text(name, s_alert_names[i]);
        lv_obj_set_style_text_font(name, &ui_font_Chinese16, LV_PART_MAIN);
        lv_obj_set_style_text_color(name, ui_theme_color_lv(UI_COLOR_TEXT_SECONDARY), LV_PART_MAIN);
        lv_obj_align(name, LV_ALIGN_CENTER, -92, y);

        lv_obj_t *button = lv_btn_create(ui_ScreenPageAlertMedia);
        lv_obj_set_size(button, 188, 27);
        lv_obj_align(button, LV_ALIGN_CENTER, 54, y);
        lv_obj_set_style_bg_color(button, ui_theme_color_lv(UI_COLOR_PANEL), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(button, ui_theme_color_lv(UI_COLOR_TEXT_SECONDARY), LV_PART_MAIN);
        lv_obj_set_style_radius(button, 5, LV_PART_MAIN);
        lv_obj_set_style_pad_all(button, 2, LV_PART_MAIN);
        lv_obj_clear_flag(button, LV_OBJ_FLAG_GESTURE_BUBBLE);
        s_value_labels[i] = lv_label_create(button);
        lv_obj_set_width(s_value_labels[i], 176);
        lv_label_set_long_mode(s_value_labels[i], LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(s_value_labels[i], LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_font(s_value_labels[i], &ui_font_Chinese16, LV_PART_MAIN);
        lv_obj_set_style_text_color(s_value_labels[i], ui_theme_color_lv(UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
        lv_obj_center(s_value_labels[i]);
        lv_obj_add_event_cb(button, on_alert_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
    }

    lv_obj_t *hint = lv_label_create(ui_ScreenPageAlertMedia);
    lv_label_set_text(hint, "上滑设置 · 下滑多联表");
    lv_obj_set_style_text_font(hint, &ui_font_Chinese16, LV_PART_MAIN);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x666666), LV_PART_MAIN);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 125);

    s_refresh_timer = lv_timer_create(on_refresh_timer, 500, NULL);
    ui_nav_attach_gesture(ui_ScreenPageAlertMedia, UI_NAV_PAGE_ALERT_MEDIA);
    lv_obj_add_event_cb(ui_ScreenPageAlertMedia, on_screen_loaded, LV_EVENT_SCREEN_LOADED, NULL);
    lv_obj_add_event_cb(ui_ScreenPageAlertMedia, on_screen_delete, LV_EVENT_DELETE, NULL);
    refresh_values();
    refresh_status();
}
