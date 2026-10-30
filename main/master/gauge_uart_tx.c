#include "gauge_uart_tx.h"

#include "common/gauge_link_protocol.h"
#include "app_obd_dsp/obd_data_cache.h"
#include "app_obd_dsp/obd_tpms_cache.h"
#include "bsp_obd_dsp/nvs_storage.h"

#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

#define TX_LEFT_UART   UART_NUM_1
#define TX_RIGHT_UART  UART_NUM_2
#define TX_LEFT_GPIO   43
#define TX_RIGHT_GPIO  44
#define TX_QUEUE_BYTES 512

static const char *TAG = "gauge_uart_tx";
static TaskHandle_t s_task;
static uint32_t s_data_sequence;
static uint32_t s_config_sequence;
static bool s_started;

static esp_err_t init_port(uart_port_t port, int tx_gpio)
{
    const uart_config_t cfg = {
        .baud_rate = GAUGE_LINK_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t err = uart_param_config(port, &cfg);
    if (err != ESP_OK) return err;
    err = uart_set_pin(port, tx_gpio, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) return err;
    return uart_driver_install(port, TX_QUEUE_BYTES, 0, 0, NULL, 0);
}

static bool channel_valid(const obd_data_channel_state_t *state, obd_data_channel_t channel,
                          int64_t now_us)
{
    return state[channel].valid && state[channel].updated_us > 0 &&
           now_us - state[channel].updated_us <= 2000000;
}

static void fill_data(gauge_link_data_t *out)
{
    memset(out, 0, sizeof(*out));
    obd_data_snapshot_t obd;
    obd_tpms_snapshot_t tpms;
    obd_data_get_snapshot(&obd);
    obd_tpms_cache_get_snapshot(&tpms);
    int64_t now = esp_timer_get_time();
    out->rpm = obd.rpm;
    out->speed_kmh = obd.speed;
    out->coolant_temp_c = obd.coolant_temp;
    out->oil_temp_c = obd.oil_temp;
    out->intake_temp_c = obd.intake_temp;
    out->battery_mv = obd.bat_mv;
    out->fuel_pct = obd.fuel_pct;
    if (channel_valid(obd.state, OBD_DATA_RPM, now)) out->valid_mask |= GAUGE_LINK_VALID_RPM;
    if (channel_valid(obd.state, OBD_DATA_SPEED, now)) out->valid_mask |= GAUGE_LINK_VALID_SPEED;
    if (channel_valid(obd.state, OBD_DATA_COOLANT, now)) out->valid_mask |= GAUGE_LINK_VALID_TEMP;
    if (channel_valid(obd.state, OBD_DATA_OIL, now)) out->valid_mask |= GAUGE_LINK_VALID_OIL_TEMP;
    if (channel_valid(obd.state, OBD_DATA_INTAKE, now)) out->valid_mask |= GAUGE_LINK_VALID_INTAKE_TEMP;
    if (channel_valid(obd.state, OBD_DATA_BATTERY, now)) out->valid_mask |= GAUGE_LINK_VALID_VOLTAGE;
    if (channel_valid(obd.state, OBD_DATA_FUEL, now)) out->valid_mask |= GAUGE_LINK_VALID_FUEL;
    const uint16_t bits[] = {GAUGE_LINK_VALID_TPMS_FL, GAUGE_LINK_VALID_TPMS_FR,
                             GAUGE_LINK_VALID_TPMS_RL, GAUGE_LINK_VALID_TPMS_RR};
    for (int i = 0; i < 4; ++i) {
        out->tpms_bar_x100[i] = tpms.pressure_bar_x100[i];
        if (tpms.valid[i] && now - tpms.updated_us[i] <= 4000000) out->valid_mask |= bits[i];
    }
}

static void write_frame(uart_port_t port, uint8_t target, uint8_t type,
                        uint32_t seq, const void *payload, uint16_t length)
{
    uint8_t frame[96];
    size_t size = gauge_link_encode(type, target, seq, payload, length, frame, sizeof(frame));
    if (size) uart_write_bytes(port, frame, size);
}

static void send_config_for(uart_port_t port, uint8_t target, uint8_t dashboard_id)
{
    gauge_link_config_t cfg = {.dashboard_id = dashboard_id};
    write_frame(port, target, GAUGE_LINK_FRAME_CONFIG, ++s_config_sequence, &cfg, sizeof(cfg));
}

void gauge_uart_tx_send_config_now(void)
{
    nvs_user_cfg_t cfg;
    if (nvs_cfg_get_snapshot(&cfg) != ESP_OK) return;
    send_config_for(TX_LEFT_UART, GAUGE_LINK_TARGET_LEFT, cfg.left_dashboard_id);
    send_config_for(TX_RIGHT_UART, GAUGE_LINK_TARGET_RIGHT, cfg.right_dashboard_id);
}

static void gauge_uart_tx_task(void *arg)
{
    (void)arg;
    int64_t last_config_us = 0;
    for (;;) {
        int64_t now = esp_timer_get_time();
        if (last_config_us == 0 || now - last_config_us >= 1000000) {
            gauge_uart_tx_send_config_now();
            last_config_us = now;
        }
        gauge_link_data_t data;
        fill_data(&data);
        write_frame(TX_LEFT_UART, GAUGE_LINK_TARGET_LEFT, GAUGE_LINK_FRAME_DATA,
                    ++s_data_sequence, &data, sizeof(data));
        write_frame(TX_RIGHT_UART, GAUGE_LINK_TARGET_RIGHT, GAUGE_LINK_FRAME_DATA,
                    s_data_sequence, &data, sizeof(data));
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

esp_err_t gauge_uart_tx_start(void)
{
    if (s_started) return ESP_OK;
    esp_err_t err = init_port(TX_LEFT_UART, TX_LEFT_GPIO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART1 init failed: %s", esp_err_to_name(err));
        return err;
    }
    err = init_port(TX_RIGHT_UART, TX_RIGHT_GPIO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART2 init failed: %s", esp_err_to_name(err));
        uart_driver_delete(TX_LEFT_UART);
        return err;
    }
    if (xTaskCreate(gauge_uart_tx_task, "gauge_uart_tx", 4096, NULL,
                    tskIDLE_PRIORITY + 3, &s_task) != pdPASS) {
        uart_driver_delete(TX_LEFT_UART);
        uart_driver_delete(TX_RIGHT_UART);
        return ESP_ERR_NO_MEM;
    }
    s_started = true;
    ESP_LOGI(TAG, "dual UART link ready: GPIO%d->left, GPIO%d->right @ %u",
             TX_LEFT_GPIO, TX_RIGHT_GPIO, GAUGE_LINK_BAUDRATE);
    return ESP_OK;
}
