#include "ui_usb_storage.h"

#include "ui.h"
#include "ui_navigation.h"
#include "app_media/usb_sd_storage.h"

lv_obj_t *ui_ScreenPageUSBSettings;

static lv_obj_t *s_status;
static lv_obj_t *s_exit;
static uint32_t s_warning_until;
static int s_last_state = -1;
static esp_err_t s_last_error;

static void exit_clicked(lv_event_t *event)
{
    (void)event;
    if (usb_sd_request_exit() == ESP_ERR_INVALID_STATE) {
        s_warning_until = lv_tick_get() + 3000;
        s_last_state = -1;
        lv_label_set_text(s_status, "请先在电脑上弹出 SD 卡");
    }
}

static void refresh(lv_timer_t *timer)
{
    (void)timer;
    usb_sd_snapshot_t snapshot = usb_sd_get_snapshot();
    if (s_warning_until && (int32_t)(s_warning_until - lv_tick_get()) > 0) return;
    s_warning_until = 0;
    if (s_last_state == (int)snapshot.state && s_last_error == snapshot.error) return;
    s_last_state = snapshot.state;
    s_last_error = snapshot.error;
    const bool busy = snapshot.state == USB_SD_STARTING || snapshot.state == USB_SD_STOPPING;
    if (busy) lv_obj_add_state(s_exit, LV_STATE_DISABLED);
    else lv_obj_clear_state(s_exit, LV_STATE_DISABLED);
    switch (snapshot.state) {
    case USB_SD_STARTING:
        lv_label_set_text(s_status, "正在连接 SD 卡...");
        break;
    case USB_SD_WAITING_HOST:
        lv_label_set_text(s_status, "等待 USB 连接");
        break;
    case USB_SD_CONNECTED:
        lv_label_set_text(s_status, "SD 卡已连接\n可在电脑上读写文件");
        break;
    case USB_SD_EJECTED:
        lv_label_set_text(s_status, "SD 卡已安全弹出\n可切换普通模式");
        break;
    case USB_SD_STOPPING:
        lv_label_set_text(s_status, "正在切换普通模式...");
        break;
    case USB_SD_ERROR:
        lv_label_set_text_fmt(s_status, "SD / USB 初始化失败\n%s", esp_err_to_name(snapshot.error));
        break;
    }
}

static lv_obj_t *label(lv_obj_t *parent, const char *text, int y, uint32_t color)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_width(obj, 260);
    lv_label_set_text(obj, text);
    lv_obj_set_style_text_font(obj, &ui_font_Chinese16, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(obj, LV_ALIGN_CENTER, 0, y);
    return obj;
}

static void enter_usb_mode(lv_timer_t *timer)
{
    lv_timer_del(timer);
    usb_sd_reboot_into_transfer();
}

static void on_usb_mode(lv_event_t *event)
{
    lv_obj_t *button = lv_event_get_target(event);
    if (!lv_timer_create(enter_usb_mode, 200, NULL)) return;
    lv_obj_add_state(button, LV_STATE_DISABLED);
    lv_label_set_text(lv_obj_get_child(button, 0), "正在切换...");
}

void ui_usb_settings_screen_init(void)
{
    ui_ScreenPageUSBSettings = lv_obj_create(NULL);
    lv_obj_t *screen = ui_ScreenPageUSBSettings;
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    ui_helpers_style_screen_bg(screen);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    label(screen, "USB 模式", -100, 0xFFFFFF);
    label(screen, "当前为普通模式\n切换后重启设备", -44, 0xFFFFFF);
    label(screen, "SD 卡连接电脑读写文件\n传输期间暂停仪表和媒体", 10, 0xAAAAAA);
    const bool supported = usb_sd_supported();
    lv_obj_t *button = lv_btn_create(screen);
    lv_obj_set_size(button, 178, 38);
    lv_obj_align(button, LV_ALIGN_CENTER, 0, 76);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x225599), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_radius(button, 10, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    // Swiping over the button navigates without triggering a mode change.
    lv_obj_t *text = lv_label_create(button);
    lv_label_set_text(text, supported ? "切换 USB 模式" : "USB 未启用");
    lv_obj_set_style_text_font(text, &ui_font_Chinese16, 0);
    lv_obj_set_style_text_color(text, lv_color_white(), 0);
    lv_obj_center(text);
    if (supported) lv_obj_add_event_cb(button, on_usb_mode, LV_EVENT_CLICKED, NULL);
    else lv_obj_add_state(button, LV_STATE_DISABLED);
    label(screen, "下滑返回蓝牙", 128, 0xAAAAAA);
    ui_nav_attach_gesture(screen, UI_NAV_PAGE_USB_SETTINGS);
}

void ui_usb_storage_show(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    label(screen, "USB 模式", -100, 0xFFFFFF);
    s_status = label(screen, "正在连接 SD 卡...", -44, 0xFFFFFF);
    label(screen, "退出前请在电脑安全弹出 SD 卡\n传输期间暂停仪表和媒体", 26, 0xAAAAAA);
    s_exit = lv_btn_create(screen);
    lv_obj_set_size(s_exit, 164, 38);
    lv_obj_align(s_exit, LV_ALIGN_CENTER, 0, 96);
    lv_obj_set_style_bg_color(s_exit, lv_color_hex(0x225599), 0);
    lv_obj_set_style_bg_opa(s_exit, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_exit, 0, 0);
    lv_obj_set_style_radius(s_exit, 12, 0);
    lv_obj_t *text = lv_label_create(s_exit);
    lv_obj_set_style_text_font(text, &ui_font_Chinese16, 0);
    lv_obj_set_style_text_color(text, lv_color_white(), 0);
    lv_label_set_text(text, "普通模式");
    lv_obj_center(text);
    lv_obj_add_event_cb(s_exit, exit_clicked, LV_EVENT_CLICKED, NULL);
    lv_timer_create(refresh, 200, NULL);
    refresh(NULL);
    lv_disp_load_scr(screen);
}
