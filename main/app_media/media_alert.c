#include "app_media/media_alert.h"

#include <stddef.h>

#include "esp_log.h"
#include "esp_timer.h"

#include "app_media/sd_media_manager.h"
#include "app_media/wav_player.h"

static const char *TAG = "media_alert";

#define MEDIA_ALERT_COOLDOWN_US (5LL * 1000LL * 1000LL)

/*
 * Alert files are intentionally kept on the SD card so they can be replaced
 * without rebuilding the firmware. Files use the WAV format accepted by the
 * player: PCM, 16-bit, 16 kHz, mono or stereo.
 */
static const char *const s_alert_paths[MEDIA_ALERT_COUNT] = {
    [MEDIA_ALERT_TPMS_LOW] = "/sdcard/ALERT/TPMSLOW.WAV",
    [MEDIA_ALERT_TPMS_LEAK] = "/sdcard/ALERT/TPMSLEAK.WAV",
    [MEDIA_ALERT_TPMS_LOST] = "/sdcard/ALERT/TPMSLOST.WAV",
    [MEDIA_ALERT_BATTERY_LOW] = "/sdcard/ALERT/BATLOW.WAV",
    [MEDIA_ALERT_TEMP_HIGH] = "/sdcard/ALERT/TEMPHIGH.WAV",
};

static int64_t s_last_play_us[MEDIA_ALERT_COUNT];

esp_err_t media_alert_notify(media_alert_type_t type)
{
    if (type < 0 || type >= MEDIA_ALERT_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    int64_t now_us = esp_timer_get_time();
    if (s_last_play_us[type] != 0 && now_us - s_last_play_us[type] < MEDIA_ALERT_COOLDOWN_US) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!sd_media_is_ready() || !s_alert_paths[type] ||
        !sd_media_file_exists(s_alert_paths[type])) {
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t err = wav_player_play(s_alert_paths[type]);
    if (err == ESP_OK) {
        s_last_play_us[type] = now_us;
    } else if (err != ESP_ERR_TIMEOUT) {
        ESP_LOGD(TAG, "Cannot queue %s: %s", s_alert_paths[type], esp_err_to_name(err));
    }
    return err;
}
