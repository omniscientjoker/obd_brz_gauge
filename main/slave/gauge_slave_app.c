#include "gauge_uart_rx.h"
#include "common/gauge_link_protocol.h"

#include "bsp_obd_dsp/i2c_driver/I2C_Driver.h"
#include "bsp_obd_dsp/exio/TCA9554PWR.h"
#include "bsp_obd_dsp/lcd_driver/ST77916.h"
#include "bsp_obd_dsp/nvs_storage.h"
#include "app_obd_dsp/obd_data_cache.h"
#include "app_obd_dsp/obd_tpms_cache.h"
#include "export_path/ui.h"
#include "export_path/ui_navigation.h"

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "lvgl.h"

#include <stdio.h>
#include <assert.h>

#if CONFIG_GAUGE_FIRMWARE_SLAVE

static const char *TAG = "gauge_slave";
SemaphoreHandle_t lvgl_mux = NULL;

#define LCD_H_RES 360
#define LCD_V_RES 360
#define LVGL_TASK_MS 5

static bool lvgl_lock(int timeout_ms)
{
    if (!lvgl_mux) return false;
    return xSemaphoreTake(lvgl_mux, timeout_ms < 0 ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

static void lvgl_unlock(void)
{
    xSemaphoreGive(lvgl_mux);
}

static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io,
                                    esp_lcd_panel_io_event_data_t *edata,
                                    void *user_ctx)
{
    (void)panel_io;
    (void)edata;
    lv_disp_drv_t *driver = (lv_disp_drv_t *)user_ctx;
    lv_disp_flush_ready(driver);
    return false;
}

static void flush_cb(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)driver->user_data;
    esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_map);
}

static void rounder_cb(lv_disp_drv_t *driver, lv_area_t *area)
{
    (void)driver;
    area->x1 = (area->x1 >> 1) << 1;
    area->y1 = (area->y1 >> 1) << 1;
    area->x2 = ((area->x2 >> 1) << 1) + 1;
    area->y2 = ((area->y2 >> 1) << 1) + 1;
}

static void lvgl_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (lvgl_lock(-1)) {
            lv_timer_handler();
            lvgl_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(LVGL_TASK_MS));
    }
}

static lv_obj_t *s_wait_screen;
static lv_obj_t *s_wait_label;

static void create_wait_screen(void)
{
    s_wait_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_wait_screen);
    lv_obj_set_style_bg_color(s_wait_screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_wait_screen, LV_OPA_COVER, LV_PART_MAIN);
    s_wait_label = lv_label_create(s_wait_screen);
    lv_label_set_text(s_wait_label, "WAITING FOR MASTER");
    lv_obj_set_style_text_color(s_wait_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(s_wait_label, LV_ALIGN_CENTER, 0, 0);
    lv_scr_load(s_wait_screen);
}

static void show_dashboard(uint8_t dashboard)
{
    ui_nav_page_t page;
    switch (dashboard) {
        case GAUGE_LINK_DASH_SPEED: page = UI_NAV_PAGE_SPEED; break;
        case GAUGE_LINK_DASH_TEMP: page = UI_NAV_PAGE_TEMP; break;
        case GAUGE_LINK_DASH_TPMS: page = UI_NAV_PAGE_TPMS; break;
        case GAUGE_LINK_DASH_FUEL:
        case GAUGE_LINK_DASH_INFO: page = UI_NAV_PAGE_INFO; break;
        case GAUGE_LINK_DASH_NEEDLE: page = UI_NAV_PAGE_NEEDLE; break;
        case GAUGE_LINK_DASH_CHART: page = UI_NAV_PAGE_CHART; break;
        case GAUGE_LINK_DASH_RPM:
        default: page = UI_NAV_PAGE_RPM; break;
    }
    if (!ui_nav_show(page, LV_SCR_LOAD_ANIM_FADE_ON, 150, 0, false)) {
        ESP_LOGW(TAG, "dashboard %u could not be shown", dashboard);
    }
}

static void slave_ui_tick(lv_timer_t *timer)
{
    (void)timer;
    bool changed = false;
    uint8_t dashboard = gauge_slave_dashboard_take(&changed);
    if (changed) show_dashboard(dashboard);
    if (!gauge_slave_link_is_connected() && s_wait_screen && lv_scr_act() == s_wait_screen) {
        lv_label_set_text(s_wait_label, "WAITING FOR MASTER");
    }
}

static void init_display(void)
{
    I2C_Init();
    ESP_ERROR_CHECK(EXIO_Init());
    static lv_disp_drv_t disp_drv;
    LCD_SetFlushCallback(notify_lvgl_flush_ready, &disp_drv);
    ST77916_Init();
    Backlight_Init();

    lv_init();
    size_t pixels = LCD_H_RES * 40;
    lv_color_t *buf1 = heap_caps_malloc(pixels * sizeof(lv_color_t), MALLOC_CAP_DMA);
    lv_color_t *buf2 = heap_caps_malloc(pixels * sizeof(lv_color_t), MALLOC_CAP_DMA);
    if (!buf1 || !buf2) {
        heap_caps_free(buf1);
        heap_caps_free(buf2);
        pixels = LCD_H_RES * 20;
        buf1 = heap_caps_malloc(pixels * sizeof(lv_color_t), MALLOC_CAP_DMA);
        buf2 = heap_caps_malloc(pixels * sizeof(lv_color_t), MALLOC_CAP_DMA);
    }
    assert(buf1 && buf2);
    static lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, pixels);
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LCD_H_RES;
    disp_drv.ver_res = LCD_V_RES;
    disp_drv.flush_cb = flush_cb;
    disp_drv.rounder_cb = rounder_cb;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.user_data = panel_handle;
    lv_disp_drv_register(&disp_drv);
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_storage_init());
    obd_data_reset();
    obd_tpms_cache_reset();
    init_display();

    lvgl_mux = xSemaphoreCreateMutex();
    assert(lvgl_mux);
    xTaskCreate(lvgl_task, "LVGL", 8192, NULL, 4, NULL);

    /* ui_init registers the same dashboard renderers as the master. The slave
       never initializes touch, OBD, BLE, SD or media services. */
    if (lvgl_lock(-1)) {
        ui_init();
        create_wait_screen();
        lv_timer_create(slave_ui_tick, 100, NULL);
        lvgl_unlock();
    }
    ESP_ERROR_CHECK(gauge_uart_rx_start());
    ESP_LOGI(TAG, "slave ready; waiting for CONFIG/DATA frames");
}

#endif
