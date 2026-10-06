#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "vehicle_custom_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OBD_TPMS_WHEEL_COUNT 4

typedef enum {
    OBD_TPMS_FL = 0,
    OBD_TPMS_FR,
    OBD_TPMS_RL,
    OBD_TPMS_RR,
} obd_tpms_wheel_t;

typedef struct {
    int16_t pressure_bar_x100[OBD_TPMS_WHEEL_COUNT];
    int64_t updated_us[OBD_TPMS_WHEEL_COUNT];
    bool valid[OBD_TPMS_WHEEL_COUNT];
} obd_tpms_snapshot_t;

void obd_tpms_cache_reset(void);
void obd_tpms_cache_set(obd_tpms_wheel_t wheel, float pressure_bar,
                        int64_t now_us);
void obd_tpms_cache_invalidate(obd_tpms_wheel_t wheel);
void obd_tpms_cache_expire(int64_t now_us, int64_t max_age_us);
bool obd_tpms_cache_get(obd_tpms_wheel_t wheel, int16_t *pressure_bar_x100,
                        int64_t *updated_us);
void obd_tpms_cache_get_snapshot(obd_tpms_snapshot_t *out);

#ifdef __cplusplus
}
#endif
