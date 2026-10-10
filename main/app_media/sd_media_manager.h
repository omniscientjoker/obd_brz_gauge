#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* FATFS is configured without long-file-name support. */
#define SD_MEDIA_RESOURCE_MAX 16
#define SD_MEDIA_RESOURCE_NAME_MAX 13

typedef enum {
    SD_MEDIA_STATE_NO_CARD = 0,
    SD_MEDIA_STATE_MOUNTING,
    SD_MEDIA_STATE_READY,
    SD_MEDIA_STATE_ERROR,
} sd_media_state_t;

typedef struct {
    sd_media_state_t state;
    bool indexing;
    uint8_t audio_count;
    uint8_t video_count;
    char audio[SD_MEDIA_RESOURCE_MAX][SD_MEDIA_RESOURCE_NAME_MAX];
    char video[SD_MEDIA_RESOURCE_MAX][SD_MEDIA_RESOURCE_NAME_MAX];
} sd_media_resource_snapshot_t;

/* Start the non-blocking SDMMC 4-bit mount task. Safe to call once. */
esp_err_t sd_media_init(void);

/* Returns true only after /sdcard has been mounted successfully. */
bool sd_media_is_ready(void);

/* Check a resource without exposing the mount implementation to callers. */
bool sd_media_file_exists(const char *path);

/* Request an asynchronous scan of /sdcard/ALERT. */
void sd_media_request_resource_scan(void);
void sd_media_get_resource_snapshot(sd_media_resource_snapshot_t *out);

/* Select the SD boot animation when both files are present. */
bool sd_media_get_boot_video_paths(const char **manifest_path, const char **data_path);

/* Serialize media file access with an SD unmount/retry operation. */
bool sd_media_lock(int timeout_ms);
void sd_media_unlock(void);

#ifdef __cplusplus
}
#endif
