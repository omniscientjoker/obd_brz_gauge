#include "app_media/media_alert.h"

#include <stddef.h>
#include <stdio.h>
#include <strings.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"

#include "app_media/sd_media_manager.h"
#include "app_media/wav_player.h"
#include "bsp_obd_dsp/nvs_storage.h"

static const char *TAG = "media_alert";

#define MEDIA_ALERT_COOLDOWN_US (5LL * 1000LL * 1000LL)
#define MEDIA_ALERT_PATH_MAX 160

/*
 * Alert files are intentionally kept on the SD card so they can be replaced
 * without rebuilding the firmware. Files use the WAV format accepted by the
 * player: PCM, 16-bit, 16 kHz, mono or stereo.
 */
static int64_t s_last_play_us[MEDIA_ALERT_COUNT];
static media_alert_video_handler_t s_video_handler;

void media_alert_set_video_handler(media_alert_video_handler_t handler)
{
    s_video_handler = handler;
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
        return snprintf(path_a, path_a_len, "/sdcard/ALERT/%s", name) > 0;
    }
    if (cfg->mode[type] == NVS_MEDIA_ALERT_VIDEO) {
        const char *ext = strrchr(name, '.');
        if (!ext || strcasecmp(ext, ".TXT") != 0) return false;
        size_t stem_len = (size_t)(ext - name);
        if (stem_len == 0) return false;
        return snprintf(path_a, path_a_len, "/sdcard/ALERT/%s", name) > 0 &&
               snprintf(path_b, path_b_len, "/sdcard/ALERT/%.*s.BIN",
                        (int)stem_len, name) > 0;
    }
    return false;
}

esp_err_t media_alert_notify(media_alert_type_t type)
{
    if (type < 0 || type >= MEDIA_ALERT_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    int64_t now_us = esp_timer_get_time();
    if (s_last_play_us[type] != 0 && now_us - s_last_play_us[type] < MEDIA_ALERT_COOLDOWN_US) {
        return ESP_ERR_INVALID_STATE;
    }
    nvs_media_alert_cfg_t cfg = {0};
    if (nvs_media_alert_cfg_get(&cfg) != ESP_OK || !sd_media_is_ready()) {
        return ESP_ERR_NOT_FOUND;
    }
    char path_a[MEDIA_ALERT_PATH_MAX] = {0};
    char path_b[MEDIA_ALERT_PATH_MAX] = {0};
    if (!build_resource_paths(type, &cfg, path_a, sizeof(path_a), path_b, sizeof(path_b)) ||
        !sd_media_file_exists(path_a) ||
        (cfg.mode[type] == NVS_MEDIA_ALERT_VIDEO && !sd_media_file_exists(path_b))) {
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t err;
    if (cfg.mode[type] == NVS_MEDIA_ALERT_AUDIO) {
        err = wav_player_play(path_a);
    } else if (s_video_handler) {
        err = s_video_handler(path_a, path_b);
    } else {
        err = ESP_ERR_NOT_SUPPORTED;
    }
    if (err == ESP_OK) {
        s_last_play_us[type] = now_us;
    } else if (err != ESP_ERR_TIMEOUT) {
        ESP_LOGD(TAG, "Cannot play %s: %s", path_a, esp_err_to_name(err));
    }
    return err;
}
