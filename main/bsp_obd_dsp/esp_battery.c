#include "bsp_obd_dsp/esp_battery.h"

#include "bsp_obd_dsp/i2c_driver/I2C_Driver.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdbool.h>

#define BQ27220_I2C_ADDRESS       0x55
#define BQ27220_VOLTAGE_COMMAND   0x08
#define BQ27220_SOC_COMMAND       0x2C
#define ESP_BATTERY_POLL_MS       1000

static const char *TAG = "esp_battery";
static portMUX_TYPE s_snapshot_lock = portMUX_INITIALIZER_UNLOCKED;
static volatile int32_t s_voltage_mv = -1;
static volatile int s_percent = -1;
static TaskHandle_t s_task;

static bool read_word(uint8_t command, uint16_t *value)
{
    uint8_t raw[2] = {0};
    if (I2C_Read(BQ27220_I2C_ADDRESS, command, raw, sizeof(raw)) != ESP_OK) {
        return false;
    }
    *value = (uint16_t)raw[0] | ((uint16_t)raw[1] << 8);
    return true;
}

static void esp_battery_task(void *arg)
{
    (void)arg;
    bool announced = false;
    uint32_t failed_reads = 0;

    for (;;) {
        uint16_t voltage_raw = 0;
        uint16_t soc_raw = 0;
        bool voltage_ok = read_word(BQ27220_VOLTAGE_COMMAND, &voltage_raw);
        bool soc_ok = read_word(BQ27220_SOC_COMMAND, &soc_raw);
        int32_t voltage_mv = (voltage_ok && voltage_raw > 0 && voltage_raw < 7000)
                           ? (int32_t)voltage_raw : -1;
        int percent = (soc_ok && soc_raw <= 100) ? (int)soc_raw : -1;

        portENTER_CRITICAL(&s_snapshot_lock);
        s_voltage_mv = voltage_mv;
        s_percent = percent;
        portEXIT_CRITICAL(&s_snapshot_lock);

        if (voltage_mv > 0 && percent >= 0) {
            if (!announced) {
                ESP_LOGI(TAG, "BQ27220 detected: %ld mV, %d%%", (long)voltage_mv, percent);
                announced = true;
            }
            failed_reads = 0;
        } else {
            failed_reads++;
            if (failed_reads == 1 || (failed_reads % 30) == 0) {
                ESP_LOGW(TAG, "BQ27220 read failed (voltage=%s, soc=%s)",
                         voltage_ok ? "invalid" : "error",
                         soc_ok ? "invalid" : "error");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(ESP_BATTERY_POLL_MS));
    }
}

void esp_battery_start(void)
{
    if (s_task != NULL) {
        return;
    }
    BaseType_t result = xTaskCreate(esp_battery_task, "esp_battery", 3072, NULL,
                                    tskIDLE_PRIORITY + 1, &s_task);
    if (result != pdPASS) {
        s_task = NULL;
        ESP_LOGE(TAG, "failed to start BQ27220 task");
    }
}

void esp_battery_get_snapshot(int32_t *voltage_mv, int *percent)
{
    int32_t voltage = -1;
    int charge = -1;

    portENTER_CRITICAL(&s_snapshot_lock);
    voltage = s_voltage_mv;
    charge = s_percent;
    portEXIT_CRITICAL(&s_snapshot_lock);

    if (voltage_mv != NULL) {
        *voltage_mv = voltage;
    }
    if (percent != NULL) {
        *percent = charge;
    }
}
