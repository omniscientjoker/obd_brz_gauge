#include "app_media/media_alert.h"

#include <stddef.h>
#include <stdio.h>
#include <strings.h>
#include <string.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/portmacro.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "app_media/sd_media_manager.h"
#include "app_media/wav_player.h"
#include "app_obd_dsp/obd_data_cache.h"
#include "app_obd_dsp/obd_tpms_cache.h"
#include "bsp_obd_dsp/nvs_storage.h"

static const char *TAG = "media_alert";

#define MEDIA_ALERT_QUEUE_DEPTH       8
#define MEDIA_ALERT_PATH_MAX          160
#define MEDIA_ALERT_MONITOR_PERIOD_MS 200
#define MEDIA_ALERT_DATA_MAX_AGE_US   (4LL * 1000LL * 1000LL)
#define MEDIA_ALERT_FUEL_LOW_PCT      15
#define MEDIA_ALERT_TASK_STACK        4096

typedef struct {
    media_alert_type_t type;
} media_alert_job_t;

static QueueHandle_t s_queue;
static TaskHandle_t s_dispatch_task;
static TaskHandle_t s_monitor_task;
static portMUX_TYPE s_state_mux = portMUX_INITIALIZER_UNLOCKED;
static bool s_alert_latched[MEDIA_ALERT_COUNT];
static bool s_monitor_suspended;
static media_alert_video_handler_t s_video_handler;
static media_alert_video_busy_handler_t s_video_busy_handler;

/* lvgl_mux is created before media services are started in app_main(). */
extern SemaphoreHandle_t lvgl_mux;

void media_alert_set_video_handler(media_alert_video_handler_t handler)
{
    s_video_handler = handler;
}

void media_alert_set_video_busy_handler(media_alert_video_busy_handler_t handler)
{
    s_video_busy_handler = handler;
}

void media_alert_set_monitor_suspended(bool suspended)
{
    portENTER_CRITICAL(&s_state_mux);
    s_monitor_suspended = suspended;
    portEXIT_CRITICAL(&s_state_mux);
}

static bool build_resource_paths(media_alert_type_t type, const nvs_media_alert_cfg_t *cfg,
                                 char *path_a, size_t path_a_len,
                                 char *path_b, size_t path_b_len)
{
    if (!cfg || !path_a || type >= MEDIA_ALERT_COUNT ||
        cfg->resource[type][0] == '\0') {
        return false;
    }
    const char *name = cfg->resource[type];
    if (strstr(name, "/") || strstr(name, "..")) return false;

    if (cfg->mode[type] == NVS_MEDIA_ALERT_AUDIO) {
        const char *ext = strrchr(name, '.');
        if (!ext || strcasecmp(ext, ".WAV") != 0) return false;
        int written = snprintf(path_a, path_a_len, "/sdcard/ALERT/%s", name);
        return written > 0 && (size_t)written < path_a_len;
    }

    if (cfg->mode[type] == NVS_MEDIA_ALERT_VIDEO) {
        const char *ext = strrchr(name, '.');
        if (!ext || strcasecmp(ext, ".TXT") != 0) return false;
        size_t stem_len = (size_t)(ext - name);
        if (stem_len == 0) return false;
        int manifest_written = snprintf(path_a, path_a_len, "/sdcard/ALERT/%s", name);
        int data_written = snprintf(path_b, path_b_len, "/sdcard/ALERT/%.*s.BIN",
                                    (int)stem_len, name);
        return manifest_written > 0 && (size_t)manifest_written < path_a_len &&
               data_written > 0 && (size_t)data_written < path_b_len;
    }
    return false;
}

/* Wait briefly for a card that is still mounting, preserving FIFO ordering. */
static bool resolve_job(const media_alert_job_t *job, uint8_t *mode,
                        char *path_a, size_t path_a_len,
                        char *path_b, size_t path_b_len)
{
    if (!job || !mode || !path_a || !path_b) return false;
    for (unsigned retry = 0; retry < 30; ++retry) {
        if (sd_media_is_ready()) {
            nvs_media_alert_cfg_t cfg = {0};
            if (nvs_media_alert_cfg_get(&cfg) == ESP_OK &&
                build_resource_paths(job->type, &cfg, path_a, path_a_len, path_b, path_b_len)) {
                *mode = cfg.mode[job->type];
                if (sd_media_file_exists(path_a) &&
                    (*mode != NVS_MEDIA_ALERT_VIDEO || sd_media_file_exists(path_b))) {
                    return true;
                }
                ESP_LOGW(TAG, "Configured resource is missing for alert %u", (unsigned)job->type);
                return false;
            }
            ESP_LOGW(TAG, "Configured resource is invalid for alert %u", (unsigned)job->type);
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    ESP_LOGW(TAG, "SD card unavailable for alert %u", (unsigned)job->type);
    return false;
}

static bool video_busy(void)
{
    return s_video_busy_handler && s_video_busy_handler();
}

static esp_err_t call_video_handler(const char *manifest_path, const char *data_path)
{
    if (!s_video_handler) return ESP_ERR_NOT_SUPPORTED;
    if (!lvgl_mux || xSemaphoreTake(lvgl_mux, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = s_video_handler(manifest_path, data_path);
    xSemaphoreGive(lvgl_mux);
    return err;
}

static void wait_for_audio_idle(void)
{
    while (wav_player_is_busy()) vTaskDelay(pdMS_TO_TICKS(20));
}

static void wait_for_video_idle(void)
{
    while (video_busy()) vTaskDelay(pdMS_TO_TICKS(33));
}

static void dispatch_job(const media_alert_job_t *job)
{
    uint8_t mode = NVS_MEDIA_ALERT_AUDIO;
    char path_a[MEDIA_ALERT_PATH_MAX] = {0};
    char path_b[MEDIA_ALERT_PATH_MAX] = {0};
    if (!resolve_job(job, &mode, path_a, sizeof(path_a), path_b, sizeof(path_b))) {
        return;
    }

    if (mode == NVS_MEDIA_ALERT_AUDIO) {
        wait_for_video_idle();
        wait_for_audio_idle();
        esp_err_t err = wav_player_play(path_a);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Alert %u queued audio: %s", (unsigned)job->type, path_a);
            wait_for_audio_idle();
        } else {
            ESP_LOGW(TAG, "Alert %u audio queue failed: %s", (unsigned)job->type,
                     esp_err_to_name(err));
        }
    } else {
        wait_for_audio_idle();
        wait_for_video_idle();
        esp_err_t err = call_video_handler(path_a, path_b);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Alert %u started video: %s", (unsigned)job->type, path_a);
            wait_for_video_idle();
        } else {
            ESP_LOGW(TAG, "Alert %u video start failed: %s", (unsigned)job->type,
                     esp_err_to_name(err));
        }
    }
}

static void media_alert_dispatch_task(void *arg)
{
    (void)arg;
    media_alert_job_t job;
    while (true) {
        if (xQueueReceive(s_queue, &job, portMAX_DELAY) == pdTRUE) {
            dispatch_job(&job);
        }
    }
}

static bool channel_fresh(const obd_data_snapshot_t *snapshot,
                          obd_data_channel_t channel, int64_t now_us)
{
    if (!snapshot || channel >= OBD_DATA_CHANNEL_COUNT) return false;
    const obd_data_channel_state_t *state = &snapshot->state[channel];
    return state->valid && state->updated_us > 0 && now_us >= state->updated_us &&
           now_us - state->updated_us <= MEDIA_ALERT_DATA_MAX_AGE_US;
}

/* Unknown/stale wheels must not clear an active alert. Trigger as soon as any
 * fresh wheel is abnormal; only treat TPMS as recovered after all four wheels
 * have supplied fresh, in-range values. */
static bool tpms_evaluate(int64_t now_us, const nvs_user_cfg_t *cfg, bool *abnormal)
{
    if (!cfg || !abnormal) return false;
    *abnormal = false;
    obd_tpms_cache_expire(now_us, MEDIA_ALERT_DATA_MAX_AGE_US);
    obd_tpms_snapshot_t snapshot;
    obd_tpms_cache_get_snapshot(&snapshot);
    unsigned fresh_count = 0;
    for (uint8_t i = 0; i < OBD_TPMS_WHEEL_COUNT; ++i) {
        const int64_t updated_us = snapshot.updated_us[i];
        const bool fresh = snapshot.valid[i] && updated_us > 0 && now_us >= updated_us &&
                           now_us - updated_us <= MEDIA_ALERT_DATA_MAX_AGE_US;
        if (!fresh) continue;
        ++fresh_count;
        if (snapshot.pressure_bar_x100[i] < (int16_t)cfg->tpms_pressure_min_bar_x100 ||
            snapshot.pressure_bar_x100[i] > (int16_t)cfg->tpms_pressure_max_bar_x100) {
            *abnormal = true;
        }
    }
    return *abnormal || fresh_count == OBD_TPMS_WHEEL_COUNT;
}

static void update_edge(media_alert_type_t type, bool state_valid, bool abnormal)
{
    /* Invalid or stale telemetry is unknown, not a recovery edge. */
    if (!state_valid) return;
    if (!abnormal) {
        s_alert_latched[type] = false;
        return;
    }
    if (s_alert_latched[type]) return;

    esp_err_t err = media_alert_notify(type);
    if (err == ESP_OK) {
        s_alert_latched[type] = true;
        ESP_LOGI(TAG, "Alert %u entered abnormal state", (unsigned)type);
    } else {
        ESP_LOGW(TAG, "Alert %u could not be queued: %s", (unsigned)type,
                 esp_err_to_name(err));
    }
}

static void media_alert_monitor_task(void *arg)
{
    (void)arg;
    while (true) {
        nvs_user_cfg_t cfg = {0};
        obd_data_snapshot_t obd = {0};
        bool suspended;
        int64_t now_us = esp_timer_get_time();
        portENTER_CRITICAL(&s_state_mux);
        suspended = s_monitor_suspended;
        portEXIT_CRITICAL(&s_state_mux);
        if (suspended) {
            vTaskDelay(pdMS_TO_TICKS(MEDIA_ALERT_MONITOR_PERIOD_MS));
            continue;
        }
        bool cfg_ok = nvs_cfg_get_snapshot(&cfg) == ESP_OK;
        obd_data_get_snapshot(&obd);

        /* Recheck after taking the snapshot: showroom may have started between
         * the first check and cache read, in which case the snapshot can
         * already contain synthetic samples. */
        portENTER_CRITICAL(&s_state_mux);
        suspended = s_monitor_suspended;
        portEXIT_CRITICAL(&s_state_mux);

        if (!suspended && cfg_ok) {
            update_edge(MEDIA_ALERT_VOLTAGE,
                        channel_fresh(&obd, OBD_DATA_BATTERY, now_us),
                        obd.bat_mv > 0 && obd.bat_mv < (int32_t)cfg.tpms_voltage_min_mv);
            update_edge(MEDIA_ALERT_TEMP_HIGH,
                        channel_fresh(&obd, OBD_DATA_COOLANT, now_us), obd.coolant_temp > 110);
            bool tpms_abnormal = false;
            const bool tpms_state_valid = tpms_evaluate(now_us, &cfg, &tpms_abnormal);
            update_edge(MEDIA_ALERT_TPMS, tpms_state_valid, tpms_abnormal);
            update_edge(MEDIA_ALERT_SPEED,
                        channel_fresh(&obd, OBD_DATA_SPEED, now_us),
                        obd.speed >= cfg.speed_max_kmh);
            update_edge(MEDIA_ALERT_FUEL_LOW,
                        channel_fresh(&obd, OBD_DATA_FUEL, now_us),
                        obd.fuel_pct >= 0 && obd.fuel_pct <= MEDIA_ALERT_FUEL_LOW_PCT);
            update_edge(MEDIA_ALERT_RPM,
                        channel_fresh(&obd, OBD_DATA_RPM, now_us),
                        obd.rpm >= cfg.rpm_warn_threshold);
        }
        vTaskDelay(pdMS_TO_TICKS(MEDIA_ALERT_MONITOR_PERIOD_MS));
    }
}

esp_err_t media_alert_init(void)
{
    if (s_queue) return ESP_OK;
    s_queue = xQueueCreate(MEDIA_ALERT_QUEUE_DEPTH, sizeof(media_alert_job_t));
    if (!s_queue) return ESP_ERR_NO_MEM;

    if (xTaskCreateWithCaps(media_alert_dispatch_task, "media_alert_tx",
                            MEDIA_ALERT_TASK_STACK, NULL, tskIDLE_PRIORITY + 2,
                            &s_dispatch_task, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreateWithCaps(media_alert_monitor_task, "media_alert_mon",
                            MEDIA_ALERT_TASK_STACK, NULL, tskIDLE_PRIORITY + 2,
                            &s_monitor_task, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        vTaskDelete(s_dispatch_task);
        s_dispatch_task = NULL;
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Alert monitor and media queue started");
    return ESP_OK;
}

esp_err_t media_alert_notify(media_alert_type_t type)
{
    if ((int)type < 0 || type >= MEDIA_ALERT_COUNT) return ESP_ERR_INVALID_ARG;
    if (!s_queue) return ESP_ERR_INVALID_STATE;

    media_alert_job_t job = {.type = type};
    return xQueueSend(s_queue, &job, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}
