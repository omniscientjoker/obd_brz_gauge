#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MEDIA_ALERT_TPMS_LOW = 0,
    MEDIA_ALERT_TPMS_LEAK,
    MEDIA_ALERT_TPMS_LOST,
    MEDIA_ALERT_BATTERY_LOW,
    MEDIA_ALERT_TEMP_HIGH,
    MEDIA_ALERT_COUNT,
} media_alert_type_t;

/* Queue an alert with duplicate suppression and a 5-second cooldown. */
esp_err_t media_alert_notify(media_alert_type_t type);

#ifdef __cplusplus
}
#endif
