#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Start the non-blocking SDMMC 4-bit mount task. Safe to call once. */
esp_err_t sd_media_init(void);

/* Returns true only after /sdcard has been mounted successfully. */
bool sd_media_is_ready(void);

/* Check a resource without exposing the mount implementation to callers. */
bool sd_media_file_exists(const char *path);

/* Select the SD boot animation when both files are present. */
bool sd_media_get_boot_video_paths(const char **manifest_path, const char **data_path);

/* Serialize media file access with an SD unmount/retry operation. */
bool sd_media_lock(int timeout_ms);
void sd_media_unlock(void);

#ifdef __cplusplus
}
#endif
