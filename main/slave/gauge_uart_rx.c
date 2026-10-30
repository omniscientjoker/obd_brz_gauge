#include "gauge_uart_rx.h"

#include "common/gauge_link_protocol.h"
#include "app_obd_dsp/obd_data_cache.h"
#include "app_obd_dsp/obd_tpms_cache.h"

#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

#define RX_UART UART_NUM_1
#define RX_GPIO 44
#define RX_BUF_SIZE 512

static const char *TAG = "gauge_uart_rx";
static gauge_link_parser_t s_parser;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static uint8_t s_dashboard = GAUGE_LINK_DASH_RPM;
static bool s_dashboard_changed;
static bool s_connected;
static int64_t s_last_data_us;
static bool s_have_data_sequence;
static uint32_t s_last_data_sequence;
static bool s_have_config_sequence;
static uint32_t s_last_config_sequence;

static bool sequence_newer(uint32_t next, uint32_t previous)
{
    return (int32_t)(next - previous) > 0;
}

static void apply_data(const gauge_link_data_t *data)
{
    uint16_t mask = data->valid_mask;
    if (mask & GAUGE_LINK_VALID_RPM) obd_data_set_rpm(data->rpm);
    else obd_data_invalidate_channel(OBD_DATA_RPM);
    if (mask & GAUGE_LINK_VALID_SPEED) obd_data_set_speed((uint8_t)(data->speed_kmh > 255 ? 255 : data->speed_kmh));
    else obd_data_invalidate_channel(OBD_DATA_SPEED);
    if (mask & GAUGE_LINK_VALID_TEMP) obd_data_set_coolant_temp(data->coolant_temp_c);
    else obd_data_set_coolant_temp_invalid();
    if (mask & GAUGE_LINK_VALID_OIL_TEMP) obd_data_set_oil_temp(data->oil_temp_c);
    else obd_data_set_oil_temp_invalid();
    if (mask & GAUGE_LINK_VALID_INTAKE_TEMP) obd_data_set_intake_temp(data->intake_temp_c);
    else obd_data_invalidate_channel(OBD_DATA_INTAKE);
    if (mask & GAUGE_LINK_VALID_VOLTAGE) obd_data_set_bat_mv(data->battery_mv);
    else obd_data_invalidate_channel(OBD_DATA_BATTERY);
    if (mask & GAUGE_LINK_VALID_FUEL) obd_data_set_fuel_pct(data->fuel_pct);
    else obd_data_invalidate_channel(OBD_DATA_FUEL);

    int64_t now = esp_timer_get_time();
    const uint16_t bits[] = {GAUGE_LINK_VALID_TPMS_FL, GAUGE_LINK_VALID_TPMS_FR,
                             GAUGE_LINK_VALID_TPMS_RL, GAUGE_LINK_VALID_TPMS_RR};
    for (int i = 0; i < 4; ++i) {
        if (mask & bits[i]) obd_tpms_cache_set((obd_tpms_wheel_t)i,
                                               data->tpms_bar_x100[i] / 100.0f, now);
        else obd_tpms_cache_invalidate((obd_tpms_wheel_t)i);
    }
}

static bool on_frame(void *ctx, const gauge_link_frame_t *frame)
{
    (void)ctx;
    if (frame->type == GAUGE_LINK_FRAME_CONFIG && frame->length == sizeof(gauge_link_config_t)) {
        if (s_have_config_sequence && !sequence_newer(frame->sequence, s_last_config_sequence)) return true;
        s_have_config_sequence = true;
        s_last_config_sequence = frame->sequence;
        const gauge_link_config_t *cfg = (const gauge_link_config_t *)frame->payload;
        if (cfg->dashboard_id < GAUGE_LINK_DASH_COUNT) {
            portENTER_CRITICAL(&s_lock);
            if (s_dashboard != cfg->dashboard_id) {
                s_dashboard = cfg->dashboard_id;
                s_dashboard_changed = true;
            }
            portEXIT_CRITICAL(&s_lock);
        }
    } else if (frame->type == GAUGE_LINK_FRAME_DATA && frame->length == sizeof(gauge_link_data_t)) {
        if (s_have_data_sequence && !sequence_newer(frame->sequence, s_last_data_sequence)) return true;
        s_have_data_sequence = true;
        s_last_data_sequence = frame->sequence;
        apply_data((const gauge_link_data_t *)frame->payload);
        portENTER_CRITICAL(&s_lock);
        s_last_data_us = esp_timer_get_time();
        s_connected = true;
        portEXIT_CRITICAL(&s_lock);
    }
    return true;
}

static void gauge_uart_rx_task(void *arg)
{
    (void)arg;
    uint8_t data[128];
    for (;;) {
        int n = uart_read_bytes(RX_UART, data, sizeof(data), pdMS_TO_TICKS(250));
        if (n > 0) gauge_link_parser_feed(&s_parser, data, (size_t)n, on_frame, NULL);
        int64_t last;
        portENTER_CRITICAL(&s_lock);
        last = s_last_data_us;
        portEXIT_CRITICAL(&s_lock);
        if (last > 0 && esp_timer_get_time() - last > 1500000) {
            obd_data_reset();
            obd_tpms_cache_reset();
            portENTER_CRITICAL(&s_lock);
            s_connected = false;
            s_last_data_us = 0;
            s_have_data_sequence = false;
            portEXIT_CRITICAL(&s_lock);
        }
    }
}

esp_err_t gauge_uart_rx_start(void)
{
    const uart_config_t cfg = {
        .baud_rate = GAUGE_LINK_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t err = uart_param_config(RX_UART, &cfg);
    if (err != ESP_OK) return err;
    err = uart_set_pin(RX_UART, UART_PIN_NO_CHANGE, RX_GPIO, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) return err;
    err = uart_driver_install(RX_UART, RX_BUF_SIZE, 0, 0, NULL, 0);
    if (err != ESP_OK) return err;
    gauge_link_parser_init(&s_parser);
    if (xTaskCreate(gauge_uart_rx_task, "gauge_uart_rx", 4096, NULL,
                    tskIDLE_PRIORITY + 3, NULL) != pdPASS) {
        uart_driver_delete(RX_UART);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "UART1 RX ready on GPIO%d @ %u", RX_GPIO, GAUGE_LINK_BAUDRATE);
    return ESP_OK;
}

uint8_t gauge_slave_dashboard_take(bool *changed)
{
    uint8_t dashboard;
    portENTER_CRITICAL(&s_lock);
    dashboard = s_dashboard;
    if (changed) *changed = s_dashboard_changed;
    s_dashboard_changed = false;
    portEXIT_CRITICAL(&s_lock);
    return dashboard;
}

bool gauge_slave_link_is_connected(void)
{
    bool connected;
    portENTER_CRITICAL(&s_lock);
    connected = s_connected;
    portEXIT_CRITICAL(&s_lock);
    return connected;
}
