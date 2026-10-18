#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MEDIA_ALERT_TPMS = 0,
    MEDIA_ALERT_VOLTAGE,
    MEDIA_ALERT_SPEED,
    MEDIA_ALERT_TEMP_HIGH,
    MEDIA_ALERT_FUEL_LOW,
    MEDIA_ALERT_RPM,
    MEDIA_ALERT_COUNT,
} media_alert_type_t;

typedef esp_err_t (*media_alert_video_handler_t)(const char *manifest_path,
                                                 const char *data_path);
typedef bool (*media_alert_video_busy_handler_t)(void);

/* Start the independent OBD monitor and serialized alert-media dispatcher. */
esp_err_t media_alert_init(void);

/* Queue one alert event. Distinct abnormal edges retain FIFO entries even if
 * an earlier event of the same type is still pending or playing. */
esp_err_t media_alert_notify(media_alert_type_t type);
void media_alert_set_video_handler(media_alert_video_handler_t handler);
void media_alert_set_video_busy_handler(media_alert_video_busy_handler_t handler);
void media_alert_set_monitor_suspended(bool suspended);

#ifdef __cplusplus
}
#endif
