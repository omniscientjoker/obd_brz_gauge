#include "app_media/sd_media_manager.h"

#include <stdio.h>
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
#define SD_MEDIA_TASK_STACK 4096
#define SD_MEDIA_RETRY_MS 5000

static SemaphoreHandle_t s_media_lock;
static TaskHandle_t s_mount_task;
static sdmmc_card_t *s_card;
static volatile bool s_ready;
static volatile bool s_started;

static bool path_is_safe(const char *path)
{
    if (!path || (strncmp(path, "/sdcard", 7) != 0) ||
        (path[7] != '/' && path[7] != '\0')) {
        return false;
    }
    return strstr(path, "..") == NULL;
}

static esp_err_t mount_once(void)
{
#if CONFIG_OBD_HW_VERSION_V1_WAVESHARE
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();

    /* These pins are the verified V1 SDMMC 4-bit wiring. */
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
    esp_err_t err = esp_vfs_fat_sdmmc_mount(SD_MEDIA_MOUNT_POINT, &host, &slot,
                                             &mount_cfg, &s_card);
    if (err == ESP_OK) {
        sdmmc_card_print_info(stdout, s_card);
    }
    return err;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

static void sd_media_mount_task(void *arg)
{
    (void)arg;
    while (true) {
        if (!s_ready) {
            esp_err_t err = mount_once();
            if (err == ESP_OK) {
                s_ready = true;
                ESP_LOGI(TAG, "SDMMC mounted at %s", SD_MEDIA_MOUNT_POINT);
            } else {
                s_card = NULL;
                ESP_LOGW(TAG, "SDMMC mount unavailable: %s; retrying", esp_err_to_name(err));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(SD_MEDIA_RETRY_MS));
    }
}

esp_err_t sd_media_init(void)
{
    if (s_started) {
        return ESP_OK;
    }
    s_started = true;
    s_media_lock = xSemaphoreCreateMutex();
    if (!s_media_lock) {
        s_started = false;
        return ESP_ERR_NO_MEM;
    }
#if CONFIG_OBD_HW_VERSION_V1_WAVESHARE
    if (xTaskCreate(sd_media_mount_task, "sd_media", SD_MEDIA_TASK_STACK, NULL,
                    tskIDLE_PRIORITY + 1, &s_mount_task) != pdPASS) {
        vSemaphoreDelete(s_media_lock);
        s_media_lock = NULL;
        s_started = false;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
#else
    ESP_LOGW(TAG, "SD media is disabled for this board pin mapping");
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

bool sd_media_is_ready(void)
{
    return s_ready;
}

bool sd_media_lock(int timeout_ms)
{
    if (!s_media_lock) {
        return false;
    }
    TickType_t timeout = timeout_ms < 0 ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(s_media_lock, timeout) == pdTRUE;
}

void sd_media_unlock(void)
{
    if (s_media_lock) {
        xSemaphoreGive(s_media_lock);
    }
}

bool sd_media_file_exists(const char *path)
{
    if (!s_ready || !path_is_safe(path) || !sd_media_lock(100)) {
        return false;
    }
    struct stat st = {0};
    bool exists = stat(path, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0;
    sd_media_unlock();
    return exists;
}

bool sd_media_get_boot_video_paths(const char **manifest_path, const char **data_path)
{
    static const char manifest[] = "/sdcard/VIDEO/BOOT.TXT";
    static const char data[] = "/sdcard/VIDEO/BOOT.BIN";
    if (!manifest_path || !data_path || !sd_media_file_exists(manifest) ||
        !sd_media_file_exists(data)) {
        return false;
    }
    *manifest_path = manifest;
    *data_path = data;
    return true;
}
