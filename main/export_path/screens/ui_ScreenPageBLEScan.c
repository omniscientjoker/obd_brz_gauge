// BLE Scan & Select Page
// Shows saved device (with delete) + a list of discovered BLE devices
//
// Two use cases depending on device_role:
//  - MASTER/STANDALONE: scan and connect to an OBD ELM327 adapter (original logic unchanged)
//  - SLAVE: scan and pair with the triple-gauge master ("SkyGauge-XXYY" broadcast), see gauge_pair_ble_client.c

#include "../ui.h"
#include "bsp_obd_dsp/elm327_ble_client.h"
#include "bsp_obd_dsp/gauge_pair_ble_client.h"
#include "bsp_obd_dsp/espnow_link.h"
#include "bsp_obd_dsp/nvs_storage.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG_BLE_UI = "ble_scan_ui";

// UI elements (local)
static lv_obj_t *s_list = NULL;             // scanned device list
static lv_obj_t *s_label_status = NULL;     // status label
static lv_obj_t *s_spinner = NULL;          // scan spinner
static lv_obj_t *s_saved_panel = NULL;      // saved device panel
static lv_obj_t *s_label_saved_hdr = NULL;  // "SAVED" sub-header
static lv_obj_t *s_saved_name_lbl = NULL;   // saved device name label
static lv_obj_t *s_label_nearby = NULL;
static lv_obj_t *s_connected_name_lbl = NULL;
static lv_obj_t *s_action_bar = NULL;
static lv_obj_t *s_action_btn = NULL;
static lv_obj_t *s_action_lbl = NULL;
static lv_obj_t *s_retry_btn = NULL;
static lv_obj_t *s_retry_lbl = NULL;
static lv_obj_t *s_action_divider = NULL;
static lv_timer_t *s_view_timer = NULL;
static bool s_scanning = false;
static bool s_slave_mode = false;           // true=slave pairing with a master, false=OBD device scan (original logic)

typedef enum {
    OBD_VIEW_NO_DEVICE = 0,
    OBD_VIEW_CONNECTING,
    OBD_VIEW_FAILED,
    OBD_VIEW_CONNECTED,
} obd_view_t;
static obd_view_t s_obd_view = OBD_VIEW_NO_DEVICE;

// Slave mode: parallel table mapping scan list button index -> corresponding device MAC (one-to-one with the s_list child order)
static uint8_t s_gauge_macs[GAUGE_PAIR_SCAN_MAX_DEVICES][6];
static int s_gauge_mac_count = 0;

// OBD device scan: same as above, records the MAC of each list item; after selection connects by exact MAC (avoids misconnecting to a same-name device)
static uint8_t s_obd_macs[BLE_SCAN_MAX_DEVICES][6];
static int s_obd_mac_count = 0;

// Forward declarations
static void start_scan(void);
static void on_device_selected(lv_event_t *e);
static void on_saved_device_delete(lv_event_t *e);
static void on_pair_result(bool ok, const char *name, const uint8_t mac[6]);
static void set_obd_scan_view(void);
static void set_obd_connecting_view(void);
static void set_obd_failed_view(void);
static void set_obd_connected_view(void);
static void refresh_obd_view(void);

static void clear_saved_obd_device(void) {
    nvs_user_cfg_t cfg = *nvs_cfg_get();
    cfg.ble_device_name[0] = '\0';
    memset(cfg.ble_obd_mac, 0, sizeof(cfg.ble_obd_mac));
    nvs_cfg_set(&cfg);
}

static void on_obd_action(lv_event_t *e) {
    (void)e;
    ESP_LOGI(TAG_BLE_UI, "Clearing OBD device from %s view", s_obd_view == OBD_VIEW_CONNECTED ? "connected" : "pending");
    elm327_ble_forget_device();
    clear_saved_obd_device();
    set_obd_scan_view();
}

static void on_obd_retry(lv_event_t *e) {
    (void)e;
    const nvs_user_cfg_t *cfg = nvs_cfg_get();
    bool has_mac = (cfg->ble_obd_mac[0] | cfg->ble_obd_mac[1] | cfg->ble_obd_mac[2] |
                    cfg->ble_obd_mac[3] | cfg->ble_obd_mac[4] | cfg->ble_obd_mac[5]) != 0;
    if (!has_mac) {
        set_obd_scan_view();
        return;
    }
    ESP_LOGI(TAG_BLE_UI, "Retrying OBD device: %s", cfg->ble_device_name);
    set_obd_connecting_view();
    elm327_ble_connect_by_addr(cfg->ble_obd_mac, cfg->ble_device_name);
}

static void obd_view_timer_cb(lv_timer_t *timer) {
    (void)timer;
    if (!s_slave_mode) refresh_obd_view();
}

static void on_screen_delete(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
    ui_ble_scan_page_leave();
    ui_ScreenPageBLEScan = NULL;
    s_list = NULL;
    s_label_status = NULL;
    s_spinner = NULL;
    s_saved_panel = NULL;
    s_label_saved_hdr = NULL;
    s_saved_name_lbl = NULL;
    s_label_nearby = NULL;
    s_connected_name_lbl = NULL;
    s_action_bar = NULL;
    s_action_btn = NULL;
    s_action_lbl = NULL;
    s_retry_btn = NULL;
    s_retry_lbl = NULL;
    s_action_divider = NULL;
    if (s_view_timer) {
        lv_timer_del(s_view_timer);
        s_view_timer = NULL;
    }
}

static void on_screen_loaded(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_SCREEN_LOADED) {
        start_scan();
    }
}

// Mutex for LVGL (defined in main)
extern SemaphoreHandle_t lvgl_mux;
static inline bool lvgl_lock_ui(int timeout_ms) {
    return xSemaphoreTake(lvgl_mux, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}
static inline void lvgl_unlock_ui(void) {
    xSemaphoreGive(lvgl_mux);
}

// BLE scan callback (called in the BT thread, LVGL must be updated thread-safely) -- OBD device scan (MASTER/STANDALONE)
static void scan_result_cb(const ble_scan_result_t *dev, int total_count) {
    if (s_obd_view != OBD_VIEW_NO_DEVICE) return;
    if (!dev) {
        if (!lvgl_lock_ui(100)) return;
        s_scanning = false;
        if (s_spinner) lv_obj_add_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);
        if (s_label_status) {
            lv_label_set_text_fmt(s_label_status,
                                   total_count > 0 ? "发现 %d 个设备" : "无设备",
                                   total_count);
        }
        lvgl_unlock_ui();
        return;
    }
    if (!s_list) return;

    if (lvgl_lock_ui(100)) {
        // Check whether a device with the same name is already in the list
        uint32_t child_cnt = lv_obj_get_child_cnt(s_list);
        for (uint32_t i = 0; i < child_cnt; i++) {
            lv_obj_t *btn = lv_obj_get_child(s_list, i);
            lv_obj_t *lbl = lv_obj_get_child(btn, 0);
            if (lbl && strcmp(lv_label_get_text(lbl), dev->name) == 0) {
                lvgl_unlock_ui();
                return; // already exists
            }
        }
        if (s_obd_mac_count >= BLE_SCAN_MAX_DEVICES) {
            lvgl_unlock_ui();
            return;
        }

        // Add new device button
        lv_obj_t *btn = lv_list_add_btn(s_list, NULL, dev->name);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x222222), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(btn, 255, LV_PART_MAIN);
        lv_obj_set_style_text_color(btn, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_set_style_text_font(btn, &ui_font_Chinese20, LV_PART_MAIN);
        lv_obj_add_event_cb(btn, on_device_selected, LV_EVENT_CLICKED, NULL);

        memcpy(s_obd_macs[s_obd_mac_count], dev->addr, 6);
        s_obd_mac_count++;

        lv_label_set_text_fmt(s_label_status, "发现 %d 个设备", total_count);
        lvgl_unlock_ui();
    }
}

// BLE scan callback -- slave pairing with a master (SLAVE); only devices with the "SkyGauge" prefix are received (see the filter in gauge_pair_ble_client.c)
static void scan_result_cb_gauge(const gauge_pair_scan_result_t *dev, int total_count) {
    if (!dev) {
        if (!lvgl_lock_ui(100)) return;
        s_scanning = false;
        if (s_spinner) lv_obj_add_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);
        if (s_label_status) {
            lv_label_set_text_fmt(s_label_status,
                                   total_count > 0 ? "发现 %d 个设备" : "未找到主机",
                                   total_count);
        }
        lvgl_unlock_ui();
        return;
    }
    if (!s_list) return;

    if (lvgl_lock_ui(100)) {
        uint32_t child_cnt = lv_obj_get_child_cnt(s_list);
        for (uint32_t i = 0; i < child_cnt; i++) {
            lv_obj_t *btn = lv_obj_get_child(s_list, i);
            lv_obj_t *lbl = lv_obj_get_child(btn, 0);
            if (lbl && strcmp(lv_label_get_text(lbl), dev->name) == 0) {
                lvgl_unlock_ui();
                return; // already exists
            }
        }
        if (s_gauge_mac_count >= GAUGE_PAIR_SCAN_MAX_DEVICES) {
            lvgl_unlock_ui();
            return;
        }

        lv_obj_t *btn = lv_list_add_btn(s_list, NULL, dev->name);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x222222), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(btn, 255, LV_PART_MAIN);
        lv_obj_set_style_text_color(btn, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_set_style_text_font(btn, &ui_font_Chinese20, LV_PART_MAIN);
        lv_obj_add_event_cb(btn, on_device_selected, LV_EVENT_CLICKED, NULL);

        memcpy(s_gauge_macs[s_gauge_mac_count], dev->addr, 6);
        s_gauge_mac_count++;

        lv_label_set_text_fmt(s_label_status, "发现 %d 个设备", total_count);
        lvgl_unlock_ui();
    }
}

// A device was tapped/selected
static void on_device_selected(lv_event_t *e) {
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *lbl = lv_obj_get_child(btn, 0);
    if (!lbl) return;

    const char *name = lv_label_get_text(lbl);

    if (s_slave_mode) {
        uint32_t idx = lv_obj_get_index(btn);
        if (idx >= (uint32_t)s_gauge_mac_count) return;
        uint8_t mac[6];
        memcpy(mac, s_gauge_macs[idx], 6);

        ESP_LOGI(TAG_BLE_UI, "Selected master: %s", name);
        gauge_pair_ble_scan_stop();
        s_scanning = false;

        lv_label_set_text(s_label_status, "连接中");
        if (s_spinner) lv_obj_clear_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);
        gauge_pair_ble_connect(mac, name, on_pair_result);
        return;
    }

    ESP_LOGI(TAG_BLE_UI, "Selected BLE device: %s", name);

    uint32_t idx = lv_obj_get_index(btn);
    if (idx >= (uint32_t)s_obd_mac_count) return;
    uint8_t mac[6];
    memcpy(mac, s_obd_macs[idx], 6);

    s_scanning = false;

    // Stop the list scan before starting the exact-MAC connection scan. The page
    // remains open while connecting, so its leave callback cannot cancel this scan.
    elm327_ble_scan_only_stop();

    nvs_user_cfg_t cfg = *nvs_cfg_get();
    strncpy(cfg.ble_device_name, name, sizeof(cfg.ble_device_name) - 1);
    cfg.ble_device_name[sizeof(cfg.ble_device_name) - 1] = '\0';
    memcpy(cfg.ble_obd_mac, mac, 6);
    nvs_cfg_set(&cfg);

    set_obd_connecting_view();
    elm327_ble_connect_by_addr(mac, name);
}

// BLE pairing result callback (called in the BT task context, lvgl_lock required)
static void on_pair_result(bool ok, const char *name, const uint8_t mac[6]) {
    if (!lvgl_lock_ui(200)) return;

    if (ok) {
        espnow_link_bind_master(mac);

        nvs_user_cfg_t cfg = *nvs_cfg_get();
        strncpy(cfg.ble_device_name, name ? name : "", sizeof(cfg.ble_device_name) - 1);
        cfg.ble_device_name[sizeof(cfg.ble_device_name) - 1] = '\0';
        nvs_cfg_set(&cfg);
        ESP_LOGI(TAG_BLE_UI, "Paired with master: %s", cfg.ble_device_name);

        if (s_saved_name_lbl) lv_label_set_text(s_saved_name_lbl, cfg.ble_device_name);
        if (s_saved_panel)    lv_obj_clear_flag(s_saved_panel,    LV_OBJ_FLAG_HIDDEN);
        if (s_label_saved_hdr) lv_obj_clear_flag(s_label_saved_hdr, LV_OBJ_FLAG_HIDDEN);

        lv_label_set_text(s_label_status, "已连接");
        ui_nav_show(UI_NAV_PAGE_TEMP, LV_SCR_LOAD_ANIM_FADE_ON, 300, 500, false);
    } else {
        ESP_LOGW(TAG_BLE_UI, "Pairing failed, rescanning");
        lv_label_set_text(s_label_status, "连接失败，重新扫描");
        // Reset the UI state before forcing a fresh scan.
        s_scanning = false;
        start_scan();
    }

    lvgl_unlock_ui();
}

// Delete the saved device
static void on_saved_device_delete(lv_event_t *e) {
    if (s_slave_mode) {
        espnow_link_unbind_master();
        nvs_user_cfg_t cfg = *nvs_cfg_get();
        cfg.ble_device_name[0] = '\0';
        nvs_cfg_set(&cfg);
        ESP_LOGI(TAG_BLE_UI, "Unbound saved master");
    } else {
        elm327_ble_forget_device();
        clear_saved_obd_device();
        ESP_LOGI(TAG_BLE_UI, "Saved BLE device cleared");
    }

    if (s_saved_panel)    lv_obj_add_flag(s_saved_panel,    LV_OBJ_FLAG_HIDDEN);
    if (s_label_saved_hdr) lv_obj_add_flag(s_label_saved_hdr, LV_OBJ_FLAG_HIDDEN);
    if (s_label_status)   lv_label_set_text(s_label_status, "已清除设备");

    if (s_slave_mode) {
        s_scanning = false;
        start_scan();         // Slave: rescan immediately after deleting the binding, so a new master can be paired
    } else {
        set_obd_scan_view();
    }
}

static void start_scan(void) {
    if (s_scanning) return;
    if (!s_slave_mode) {
        if (elm327_ble_is_connected()) {
            set_obd_connected_view();
            return;
        }
        if (elm327_ble_is_connection_failed()) {
            set_obd_failed_view();
            return;
        }
        if (elm327_ble_is_connecting()) {
            set_obd_connecting_view();
            return;
        }
    }
    s_scanning = true;

    if (s_list) lv_obj_clean(s_list);
    if (s_label_status) lv_label_set_text(s_label_status, "无设备");
    if (s_spinner) lv_obj_clear_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);

    if (s_slave_mode) {
        s_gauge_mac_count = 0;
        gauge_pair_ble_scan_start(15, scan_result_cb_gauge);
    } else {
        s_obd_mac_count = 0;
        elm327_ble_scan_only_start(15, scan_result_cb);
    }
}

void ui_ble_scan_page_leave(void) {
    if (s_slave_mode) {
        gauge_pair_ble_scan_stop();
    } else if (s_scanning) {
        elm327_ble_scan_only_stop();
    }
    s_scanning = false;
}

static void set_scan_objects_hidden(bool hidden) {
    if (s_list) {
        if (hidden) lv_obj_add_flag(s_list, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_clear_flag(s_list, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_label_nearby) {
        if (hidden) lv_obj_add_flag(s_label_nearby, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_clear_flag(s_label_nearby, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_saved_panel) lv_obj_add_flag(s_saved_panel, LV_OBJ_FLAG_HIDDEN);
    if (s_label_saved_hdr) lv_obj_add_flag(s_label_saved_hdr, LV_OBJ_FLAG_HIDDEN);
}

static void obd_action_bar_draw_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN) return;
    lv_obj_t *bar = lv_event_get_target(e);
    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(e);
    bool split = s_retry_btn && !lv_obj_has_flag(s_retry_btn, LV_OBJ_FLAG_HIDDEN);
    lv_coord_t split_x = bar->coords.x1 + 180;

    lv_draw_rect_dsc_t draw_dsc;
    lv_draw_rect_dsc_init(&draw_dsc);

    // Establish one transparent outer silhouette. The two color fills below
    // use its same bounds, so the split actions form one continuous bottom arc.
    draw_dsc.bg_opa = LV_OPA_TRANSP;
    lv_area_t silhouette = bar->coords;
    silhouette.y1 = silhouette.y2 - 44;
    draw_dsc.radius = 22;
    lv_draw_rect(draw_ctx, &draw_dsc, &silhouette);

    if (!split) {
        draw_dsc.bg_color = lv_color_hex(0xD92828);
        draw_dsc.bg_opa = LV_OPA_COVER;
        lv_draw_rect(draw_ctx, &draw_dsc, &silhouette);
        lv_area_t upper = bar->coords;
        upper.y2 = upper.y2 - 22;
        draw_dsc.radius = 0;
        lv_draw_rect(draw_ctx, &draw_dsc, &upper);
        return;
    }

    // Fill the left and right halves independently, then square off their
    // inner bottom corners so the color boundary remains vertically straight.
    draw_dsc.bg_opa = LV_OPA_COVER;
    draw_dsc.bg_color = lv_color_hex(0xD92828);
    lv_area_t red_lower = silhouette;
    red_lower.x2 = split_x - 1;
    draw_dsc.radius = 22;
    lv_draw_rect(draw_ctx, &draw_dsc, &red_lower);
    lv_area_t red_inner = red_lower;
    red_inner.x1 = LV_MAX(bar->coords.x1, split_x - 22);
    red_inner.y1 = red_inner.y2 - 22;
    draw_dsc.radius = 0;
    lv_draw_rect(draw_ctx, &draw_dsc, &red_inner);

    draw_dsc.bg_color = lv_color_hex(0x2E9D50);
    lv_area_t green_lower = silhouette;
    green_lower.x1 = split_x;
    draw_dsc.radius = 22;
    lv_draw_rect(draw_ctx, &draw_dsc, &green_lower);
    lv_area_t green_inner = green_lower;
    green_inner.x2 = LV_MIN(split_x + 22, bar->coords.x2);
    green_inner.y1 = green_inner.y2 - 22;
    draw_dsc.radius = 0;
    lv_draw_rect(draw_ctx, &draw_dsc, &green_inner);

    lv_area_t red_upper = bar->coords;
    red_upper.x2 = split_x - 1;
    red_upper.y2 = red_upper.y2 - 22;
    draw_dsc.bg_color = lv_color_hex(0xD92828);
    lv_draw_rect(draw_ctx, &draw_dsc, &red_upper);
    lv_area_t green_upper = bar->coords;
    green_upper.x1 = split_x;
    green_upper.y2 = green_upper.y2 - 22;
    draw_dsc.bg_color = lv_color_hex(0x2E9D50);
    lv_draw_rect(draw_ctx, &draw_dsc, &green_upper);
}

static void set_obd_action_buttons(const char *action, bool visible,
                                   const char *retry, bool split) {
    if (!s_action_bar) return;
    if (!visible) {
        lv_obj_add_flag(s_action_bar, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(s_action_bar, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_action_lbl, action);
    lv_obj_clear_flag(s_action_btn, LV_OBJ_FLAG_HIDDEN);
    if (split) {
        lv_label_set_text(s_retry_lbl, retry);
        lv_obj_set_width(s_action_btn, 180);
        lv_obj_set_x(s_action_btn, 0);
        lv_obj_align(s_action_lbl, LV_ALIGN_RIGHT_MID, -16, 0);
        lv_obj_align(s_retry_lbl, LV_ALIGN_LEFT_MID, 16, 0);
        lv_obj_clear_flag(s_retry_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_action_divider, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_set_width(s_action_btn, 360);
        lv_obj_set_x(s_action_btn, 0);
        lv_obj_center(s_action_lbl);
        lv_obj_add_flag(s_retry_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_action_divider, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_invalidate(s_action_bar);
}

static void set_obd_center_text(const char *name, const char *status) {
    if (s_connected_name_lbl) {
        lv_label_set_text(s_connected_name_lbl, name && name[0] ? name : "OBD");
        lv_obj_align(s_connected_name_lbl, LV_ALIGN_CENTER, 0, -18);
        lv_obj_clear_flag(s_connected_name_lbl, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_label_status) {
        lv_label_set_text(s_label_status, status);
        lv_obj_align(s_label_status, LV_ALIGN_CENTER, 0, 18);
        lv_obj_clear_flag(s_label_status, LV_OBJ_FLAG_HIDDEN);
    }
}

static void set_obd_scan_view(void) {
    if (s_slave_mode) return;
    s_obd_view = OBD_VIEW_NO_DEVICE;
    if (s_connected_name_lbl) lv_obj_add_flag(s_connected_name_lbl, LV_OBJ_FLAG_HIDDEN);
    set_scan_objects_hidden(false);
    set_obd_action_buttons(NULL, false, NULL, false);
    if (s_label_status) {
        lv_label_set_text(s_label_status, "无设备");
        lv_obj_align(s_label_status, LV_ALIGN_TOP_MID, 0, 52);
    }
    if (s_label_nearby) {
        lv_label_set_text(s_label_nearby, "附近设备");
        lv_obj_align(s_label_nearby, LV_ALIGN_TOP_MID, 0, 78);
    }
    if (s_list) lv_obj_align(s_list, LV_ALIGN_TOP_MID, 0, 96);
    s_scanning = false;
    start_scan();
}

static void set_obd_connecting_view(void) {
    if (s_slave_mode) return;
    s_obd_view = OBD_VIEW_CONNECTING;
    s_scanning = false;
    set_scan_objects_hidden(true);
    set_obd_center_text(elm327_ble_get_connected_name(), "连接中");
    set_obd_action_buttons("取消", true, NULL, false);
}

static void set_obd_failed_view(void) {
    if (s_slave_mode) return;
    s_obd_view = OBD_VIEW_FAILED;
    s_scanning = false;
    set_scan_objects_hidden(true);
    set_obd_center_text(elm327_ble_get_connected_name(), "连接失败");
    set_obd_action_buttons("取消", true, "重连", true);
}

static void set_obd_connected_view(void) {
    if (s_slave_mode) return;
    s_obd_view = OBD_VIEW_CONNECTED;
    s_scanning = false;
    set_scan_objects_hidden(true);
    set_obd_center_text(elm327_ble_get_connected_name(), "已连接");
    set_obd_action_buttons("断开重置", true, NULL, false);
}

static void refresh_obd_view(void) {
    if (s_slave_mode || !ui_ScreenPageBLEScan) return;
    if (elm327_ble_is_connected()) {
        if (s_obd_view != OBD_VIEW_CONNECTED) set_obd_connected_view();
    } else if (elm327_ble_is_connection_failed()) {
        if (s_obd_view != OBD_VIEW_FAILED) set_obd_failed_view();
    } else if (elm327_ble_is_connecting()) {
        if (s_obd_view != OBD_VIEW_CONNECTING) set_obd_connecting_view();
    }
}

void ui_ScreenPageBLEScan_screen_init(void)
{
    s_slave_mode = (nvs_cfg_get()->device_role == ESPNOW_ROLE_SLAVE);

    ui_ScreenPageBLEScan = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_ScreenPageBLEScan, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(ui_ScreenPageBLEScan, 360, LV_PART_MAIN | LV_STATE_DEFAULT);
    ui_helpers_style_screen_bg(ui_ScreenPageBLEScan);
    lv_obj_set_style_bg_opa(ui_ScreenPageBLEScan, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(ui_ScreenPageBLEScan, 0, LV_PART_MAIN);


    // Title
    lv_obj_t *label_title = lv_label_create(ui_ScreenPageBLEScan);
    lv_label_set_text(label_title, "蓝牙");
    lv_obj_set_style_text_font(label_title, &ui_font_Chinese20, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_title, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 30);

    // No animated spinner in the title area.  Status text below the title
    // remains the scan/connection progress indicator.
    s_spinner = NULL;

    // Status label
    s_label_status = lv_label_create(ui_ScreenPageBLEScan);
    lv_label_set_text(s_label_status, "无设备");
    lv_obj_set_style_text_font(s_label_status, &ui_font_Chinese16, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_label_status, lv_color_hex(0xAAAAAA), LV_PART_MAIN);
    lv_obj_align(s_label_status, LV_ALIGN_TOP_MID, 0, 50);

    // ==== SAVED DEVICE SECTION ====
    const nvs_user_cfg_t *saved_cfg = nvs_cfg_get();
    bool has_saved;
    if (s_slave_mode) {
        const uint8_t *bound_mac = espnow_link_get_bound_master_mac();
        has_saved = (bound_mac[0] | bound_mac[1] | bound_mac[2] | bound_mac[3] | bound_mac[4] | bound_mac[5]) != 0;
    } else {
        // OBD pages use an explicit connection state panel. The saved row is
        // retained only for the slave pairing flow below.
        has_saved = false;
    }

    s_label_saved_hdr = lv_label_create(ui_ScreenPageBLEScan);
    lv_label_set_text(s_label_saved_hdr, "已保存设备");
    lv_obj_set_style_text_font(s_label_saved_hdr, &ui_font_Chinese16, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_label_saved_hdr, lv_color_hex(0x888888), LV_PART_MAIN);
    lv_obj_align(s_label_saved_hdr, LV_ALIGN_TOP_MID, 0, 72);
    if (!has_saved) lv_obj_add_flag(s_label_saved_hdr, LV_OBJ_FLAG_HIDDEN);

    // Saved device row: name + delete button
    s_saved_panel = lv_obj_create(ui_ScreenPageBLEScan);
    lv_obj_remove_style_all(s_saved_panel);
    lv_obj_set_size(s_saved_panel, 264, 32);
    lv_obj_align(s_saved_panel, LV_ALIGN_TOP_MID, 0, 90);
    lv_obj_set_style_bg_color(s_saved_panel, lv_color_hex(0x222222), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_saved_panel, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(s_saved_panel, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(s_saved_panel, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(s_saved_panel, LV_OBJ_FLAG_SCROLLABLE);
    if (!has_saved) lv_obj_add_flag(s_saved_panel, LV_OBJ_FLAG_HIDDEN);

    // Device name inside panel
    s_saved_name_lbl = lv_label_create(s_saved_panel);
    lv_label_set_text(s_saved_name_lbl, has_saved ? saved_cfg->ble_device_name : "");
    lv_obj_set_style_text_font(s_saved_name_lbl, &ui_font_Chinese20, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_saved_name_lbl, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_align(s_saved_name_lbl, LV_ALIGN_LEFT_MID, 4, 0);

    // Delete button inside panel
    lv_obj_t *del_btn = lv_btn_create(s_saved_panel);
    lv_obj_set_style_clip_corner(del_btn, true, 0);
    lv_obj_set_size(del_btn, 30, 24);
    lv_obj_align(del_btn, LV_ALIGN_RIGHT_MID, -2, 0);
    lv_obj_set_style_bg_color(del_btn, lv_color_hex(0xBB2222), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(del_btn, 255, LV_PART_MAIN);
    lv_obj_set_style_radius(del_btn, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(del_btn, 2, LV_PART_MAIN);
    lv_obj_t *del_lbl = lv_label_create(del_btn);
    lv_label_set_text(del_lbl, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(del_lbl, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_center(del_lbl);
    lv_obj_add_event_cb(del_btn, on_saved_device_delete, LV_EVENT_CLICKED, NULL);

    // Thin divider
    lv_obj_t *divider = lv_obj_create(ui_ScreenPageBLEScan);
    lv_obj_remove_style_all(divider);
    lv_obj_set_size(divider, 240, 1);
    lv_obj_align(divider, LV_ALIGN_TOP_MID, 0, 128);
    lv_obj_set_style_bg_color(divider, lv_color_hex(0x444444), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(divider, 255, LV_PART_MAIN);
    lv_obj_clear_flag(divider, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    if (!has_saved) lv_obj_add_flag(divider, LV_OBJ_FLAG_HIDDEN);

    // ==== NEARBY SCAN SECTION ====
    s_label_nearby = lv_label_create(ui_ScreenPageBLEScan);
    lv_label_set_text(s_label_nearby, "附近设备");
    lv_obj_set_style_text_font(s_label_nearby, &ui_font_Chinese16, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_label_nearby, lv_color_hex(0x888888), LV_PART_MAIN);
    // If there is no saved device, collapse the unused saved-device section.
    lv_obj_align(s_label_nearby, LV_ALIGN_TOP_MID, 0, has_saved ? 134 : 78);

    // Device list (scan results)
    s_list = lv_list_create(ui_ScreenPageBLEScan);
    lv_obj_set_size(s_list, 244, 132);
    lv_obj_align(s_list, LV_ALIGN_TOP_MID, 0, has_saved ? 152 : 96);
    lv_obj_set_style_bg_color(s_list, lv_color_hex(0x111111), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_list, 255, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_list, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_list, lv_color_hex(0x444444), LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_list, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(s_list, 8, LV_PART_MAIN);

    // OBD connection state panel. It replaces the scan list while an exact
    // target is connecting or already connected.
    s_connected_name_lbl = lv_label_create(ui_ScreenPageBLEScan);
    lv_obj_set_style_text_font(s_connected_name_lbl, &ui_font_Chinese20, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_connected_name_lbl, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_align(s_connected_name_lbl, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(s_connected_name_lbl, LV_OBJ_FLAG_HIDDEN);

    // Full-width bottom action bar. Its custom draw callback keeps the top edge
    // flat while rounding only the lower corners.
    s_action_bar = lv_obj_create(ui_ScreenPageBLEScan);
    lv_obj_remove_style_all(s_action_bar);
    lv_obj_set_size(s_action_bar, 360, 56);
    lv_obj_align(s_action_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(s_action_bar, obd_action_bar_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_flag(s_action_bar, LV_OBJ_FLAG_HIDDEN);

    s_action_btn = lv_btn_create(s_action_bar);
    lv_obj_remove_style_all(s_action_btn);
    lv_obj_set_size(s_action_btn, 360, 56);
    lv_obj_set_pos(s_action_btn, 0, 0);
    s_action_lbl = lv_label_create(s_action_btn);
    lv_obj_set_style_text_font(s_action_lbl, &ui_font_Chinese16, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_action_lbl, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_center(s_action_lbl);
    lv_obj_add_event_cb(s_action_btn, on_obd_action, LV_EVENT_CLICKED, NULL);

    s_retry_btn = lv_btn_create(s_action_bar);
    lv_obj_remove_style_all(s_retry_btn);
    lv_obj_set_size(s_retry_btn, 180, 56);
    lv_obj_set_pos(s_retry_btn, 180, 0);
    s_retry_lbl = lv_label_create(s_retry_btn);
    lv_obj_set_style_text_font(s_retry_lbl, &ui_font_Chinese16, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_retry_lbl, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_center(s_retry_lbl);
    lv_obj_add_event_cb(s_retry_btn, on_obd_retry, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(s_retry_btn, LV_OBJ_FLAG_HIDDEN);

    s_action_divider = lv_obj_create(s_action_bar);
    lv_obj_remove_style_all(s_action_divider);
    lv_obj_set_size(s_action_divider, 1, 56);
    lv_obj_set_pos(s_action_divider, 180, 0);
    lv_obj_set_style_bg_color(s_action_divider, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_action_divider, LV_OPA_50, LV_PART_MAIN);
    lv_obj_add_flag(s_action_divider, LV_OBJ_FLAG_HIDDEN);

    // Gesture event for navigation
    ui_nav_attach_gesture(ui_ScreenPageBLEScan, UI_NAV_PAGE_BLE_SCAN);
    lv_obj_add_event_cb(ui_ScreenPageBLEScan, on_screen_loaded, LV_EVENT_SCREEN_LOADED, NULL);
    lv_obj_add_event_cb(ui_ScreenPageBLEScan, on_screen_delete, LV_EVENT_DELETE, NULL);

    s_view_timer = lv_timer_create(obd_view_timer_cb, 250, NULL);

    // Resolve the initial OBD state after all controls exist. A saved target is
    // shown as connecting until the GATT link is ready; an active link hides the
    // list entirely.
    if (s_slave_mode) start_scan();
    else if (elm327_ble_is_connected()) set_obd_connected_view();
    else if (elm327_ble_is_connection_failed()) set_obd_failed_view();
    else if (elm327_ble_is_connecting()) set_obd_connecting_view();
    else set_obd_scan_view();
}
