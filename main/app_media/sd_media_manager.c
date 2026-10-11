#include "app_media/sd_media_manager.h"

#include <dirent.h>
#include <stdio.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#include "sdkconfig.h"

static const char *TAG = "sd_media";

#define SD_MEDIA_MOUNT_POINT "/sdcard"
#define SD_MEDIA_ALERT_DIR "/sdcard/ALERT"
#define SD_MEDIA_PATH_MAX 160
#define SD_MEDIA_TASK_STACK 4096
#define SD_MEDIA_RETRY_MS 5000

static SemaphoreHandle_t s_media_lock;
static sdmmc_card_t *s_card;
static volatile bool s_ready;
static volatile bool s_started;
static volatile sd_media_state_t s_state = SD_MEDIA_STATE_NO_CARD;
static sd_media_resource_snapshot_t s_resources = {
    .state = SD_MEDIA_STATE_NO_CARD,
};

static bool path_is_safe(const char *path)
{
    if (!path || strncmp(path, "/sdcard", 7) != 0 ||
        (path[7] != '/' && path[7] != '\0')) return false;
    return strstr(path, "..") == NULL;
}

static bool resource_name_has_extension(const char *name, const char *extension)
{
    if (!name || !extension) return false;
    size_t name_len = strnlen(name, SD_MEDIA_RESOURCE_NAME_MAX);
    size_t extension_len = strlen(extension);
    if (name_len == 0 || name_len >= SD_MEDIA_RESOURCE_NAME_MAX || name_len <= extension_len) {
        return false;
    }
    return strcasecmp(name + name_len - extension_len, extension) == 0;
}

static bool resource_name_is_hidden_metadata(const char *name)
{
    return !name || name[0] == '.';
}

static bool resource_file_is_appledouble(const char *name)
{
    if (!name) return false;

    char path[SD_MEDIA_PATH_MAX];
    int written = snprintf(path, sizeof(path), "%s/%s", SD_MEDIA_ALERT_DIR, name);
    if (written <= 0 || (size_t)written >= sizeof(path)) return false;

    FILE *file = fopen(path, "rb");
    if (!file) return false;

    uint8_t magic[4] = {0};
    bool is_appledouble = fread(magic, 1, sizeof(magic), file) == sizeof(magic) &&
                          magic[0] == 0x00 && magic[1] == 0x05 &&
                          magic[2] == 0x16 && magic[3] == 0x07;
    fclose(file);
    return is_appledouble;
}

static void sort_names(char names[][SD_MEDIA_RESOURCE_NAME_MAX], uint8_t count)
{
    for (uint8_t i = 0; i < count; ++i) {
        for (uint8_t j = i + 1; j < count; ++j) {
            if (strcasecmp(names[i], names[j]) > 0) {
                char tmp[SD_MEDIA_RESOURCE_NAME_MAX];
                memcpy(tmp, names[i], sizeof(tmp));
                memcpy(names[i], names[j], sizeof(tmp));
                memcpy(names[j], tmp, sizeof(tmp));
            }
        }
    }
}

static bool resource_name_seen(char names[][SD_MEDIA_RESOURCE_NAME_MAX], uint8_t count,
                               const char *name)
{
    for (uint8_t i = 0; i < count; ++i) {
        if (strcasecmp(names[i], name) == 0) return true;
    }
    return false;
}

static bool regular_media_file(const char *name)
{
    char path[SD_MEDIA_PATH_MAX];
    int written = snprintf(path, sizeof(path), "%s/%s", SD_MEDIA_ALERT_DIR, name);
    if (written <= 0 || (size_t)written >= sizeof(path)) return false;
    struct stat st = {0};
    return stat(path, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0;
}

static bool paired_video_exists(const char *txt_name)
{
    const char *dot = strrchr(txt_name, '.');
    if (!dot) return false;
    char bin_path[SD_MEDIA_PATH_MAX];
    int written = snprintf(bin_path, sizeof(bin_path), "%s/%.*s.BIN", SD_MEDIA_ALERT_DIR,
                           (int)(dot - txt_name), txt_name);
    struct stat st = {0};
    return written > 0 && (size_t)written < sizeof(bin_path) && stat(bin_path, &st) == 0 &&
           S_ISREG(st.st_mode) && st.st_size > 0;
}

static void scan_resources_locked(void)
{
    memset(&s_resources.audio, 0, sizeof(s_resources.audio));
    memset(&s_resources.video, 0, sizeof(s_resources.video));
    s_resources.audio_count = 0;
    s_resources.video_count = 0;
    s_resources.state = s_state;
    s_resources.indexing = true;

    DIR *dir = opendir(SD_MEDIA_ALERT_DIR);
    if (!dir) {
        s_resources.indexing = false;
        return;
    }
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        const char *name = entry->d_name;
        /* macOS writes AppleDouble companions such as ._fight.wav to FAT volumes. */
        if (resource_name_is_hidden_metadata(name)) continue;
        /* Identify AppleDouble companions even when the filesystem exposes a
         * generated short alias instead of the hidden ._ name. */
        if (resource_file_is_appledouble(name)) continue;
        if (resource_name_has_extension(name, ".WAV") && regular_media_file(name) &&
            !resource_name_seen(s_resources.audio, s_resources.audio_count, name) &&
            s_resources.audio_count < SD_MEDIA_RESOURCE_MAX) {
            strncpy(s_resources.audio[s_resources.audio_count++], name,
                    SD_MEDIA_RESOURCE_NAME_MAX - 1);
        } else if (resource_name_has_extension(name, ".TXT") && paired_video_exists(name) &&
                   !resource_name_seen(s_resources.video, s_resources.video_count, name) &&
                   s_resources.video_count < SD_MEDIA_RESOURCE_MAX) {
            strncpy(s_resources.video[s_resources.video_count++], name,
                    SD_MEDIA_RESOURCE_NAME_MAX - 1);
        }
    }
    closedir(dir);
    sort_names(s_resources.audio, s_resources.audio_count);
    sort_names(s_resources.video, s_resources.video_count);
    s_resources.indexing = false;
    ESP_LOGI(TAG, "Resource table ready: audio=%u video=%u",
             (unsigned)s_resources.audio_count, (unsigned)s_resources.video_count);
}

static void clear_resources_locked(void)
{
    memset(&s_resources, 0, sizeof(s_resources));
    s_resources.state = s_state;
}

static esp_err_t mount_once(void)
{
#if CONFIG_OBD_HW_VERSION_V1_WAVESHARE
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.clk = GPIO_NUM_15;
    slot.cmd = GPIO_NUM_14;
    slot.d0 = GPIO_NUM_16;
    slot.d1 = GPIO_NUM_17;
    slot.d2 = GPIO_NUM_12;
    slot.d3 = GPIO_NUM_13;
    slot.width = 4;
    slot.cd = SDMMC_SLOT_NO_CD;
    slot.wp = SDMMC_SLOT_NO_WP;
    slot.flags &= ~SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 8,
        .allocation_unit_size = 0,
        .disk_status_check_enable = true,
        .use_one_fat = false,
    };
    return esp_vfs_fat_sdmmc_mount(SD_MEDIA_MOUNT_POINT, &host, &slot, &mount_cfg, &s_card);
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

static void unmount_locked(void)
{
    if (s_card) {
        esp_err_t err = esp_vfs_fat_sdcard_unmount(SD_MEDIA_MOUNT_POINT, s_card);
        if (err != ESP_OK) ESP_LOGW(TAG, "SDMMC unmount failed: %s", esp_err_to_name(err));
    }
    s_card = NULL;
    s_ready = false;
    s_state = SD_MEDIA_STATE_NO_CARD;
    clear_resources_locked();
}

static void sd_media_mount_task(void *arg)
{
    (void)arg;
    while (true) {
        if (s_ready && sd_media_lock(1000)) {
            if (!s_card || sdmmc_get_status(s_card) != ESP_OK) {
                ESP_LOGW(TAG, "SD card removed or unavailable");
                unmount_locked();
            }
            sd_media_unlock();
        }
        if (!s_ready) {
            s_state = SD_MEDIA_STATE_MOUNTING;
            esp_err_t err = mount_once();
            if (err == ESP_OK) {
                if (sd_media_lock(1000)) {
                    s_ready = true;
                    s_state = SD_MEDIA_STATE_READY;
                    scan_resources_locked();
                    sd_media_unlock();
                }
                ESP_LOGI(TAG, "SDMMC mounted at %s", SD_MEDIA_MOUNT_POINT);
            } else {
                s_card = NULL;
                s_state = (err == ESP_ERR_NOT_FOUND || err == ESP_ERR_TIMEOUT) ?
                          SD_MEDIA_STATE_NO_CARD : SD_MEDIA_STATE_ERROR;
                if (sd_media_lock(1000)) {
                    clear_resources_locked();
                    sd_media_unlock();
                }
                ESP_LOGW(TAG, "SDMMC mount unavailable: %s; retrying", esp_err_to_name(err));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(SD_MEDIA_RETRY_MS));
    }
}

esp_err_t sd_media_init(void)
{
    if (s_started) return ESP_OK;
    s_started = true;
    s_media_lock = xSemaphoreCreateMutex();
    if (!s_media_lock) {
        s_started = false;
        return ESP_ERR_NO_MEM;
    }
#if CONFIG_OBD_HW_VERSION_V1_WAVESHARE
    if (xTaskCreate(sd_media_mount_task, "sd_media", SD_MEDIA_TASK_STACK, NULL,
                    tskIDLE_PRIORITY + 1, NULL) != pdPASS) {
        vSemaphoreDelete(s_media_lock);
        s_media_lock = NULL;
        s_started = false;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
#else
    s_state = SD_MEDIA_STATE_ERROR;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

bool sd_media_is_ready(void)
{
    return s_ready;
}

bool sd_media_lock(int timeout_ms)
{
    if (!s_media_lock) return false;
    TickType_t timeout = timeout_ms < 0 ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(s_media_lock, timeout) == pdTRUE;
}

void sd_media_unlock(void)
{
    if (s_media_lock) xSemaphoreGive(s_media_lock);
}

bool sd_media_file_exists(const char *path)
{
    if (!s_ready || !path_is_safe(path) || !sd_media_lock(100)) return false;
    struct stat st = {0};
    bool exists = stat(path, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0;
    sd_media_unlock();
    return exists;
}

void sd_media_request_resource_scan(void)
{
    /* Kept for callers compiled against the old API. Resources are immutable
     * for the lifetime of a mounted card and are indexed during mount only. */
}

void sd_media_get_resource_snapshot(sd_media_resource_snapshot_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!sd_media_lock(100)) {
        out->state = s_state;
        return;
    }
    *out = s_resources;
    out->state = s_state;
    sd_media_unlock();
}

bool sd_media_get_boot_video_paths(const char **manifest_path, const char **data_path)
{
    static const char manifest[] = "/sdcard/VIDEO/BOOT.TXT";
    static const char data[] = "/sdcard/VIDEO/BOOT.BIN";
    if (!manifest_path || !data_path || !sd_media_file_exists(manifest) ||
        !sd_media_file_exists(data)) return false;
    *manifest_path = manifest;
    *data_path = data;
    return true;
}
