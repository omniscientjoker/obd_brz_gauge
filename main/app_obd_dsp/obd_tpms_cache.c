#include "obd_tpms_cache.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

static obd_tpms_snapshot_t s_snapshot;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static bool valid_wheel(obd_tpms_wheel_t wheel)
{
    return wheel >= OBD_TPMS_FL && wheel < OBD_TPMS_WHEEL_COUNT;
}

void obd_tpms_cache_reset(void)
{
    portENTER_CRITICAL(&s_mux);
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    for (uint8_t i = 0; i < OBD_TPMS_WHEEL_COUNT; ++i)
        s_snapshot.pressure_bar_x100[i] = -1;
    portEXIT_CRITICAL(&s_mux);
}

void obd_tpms_cache_set(obd_tpms_wheel_t wheel, float pressure_bar,
                        int64_t now_us)
{
    if (!valid_wheel(wheel) || pressure_bar < 0.0f || pressure_bar > 20.0f)
        return;
    portENTER_CRITICAL(&s_mux);
    s_snapshot.pressure_bar_x100[wheel] = (int16_t)(pressure_bar * 100.0f + 0.5f);
    s_snapshot.updated_us[wheel] = now_us;
    s_snapshot.valid[wheel] = true;
    portEXIT_CRITICAL(&s_mux);
}

void obd_tpms_cache_invalidate(obd_tpms_wheel_t wheel)
{
    if (!valid_wheel(wheel)) return;
    portENTER_CRITICAL(&s_mux);
    s_snapshot.valid[wheel] = false;
    portEXIT_CRITICAL(&s_mux);
}

void obd_tpms_cache_expire(int64_t now_us, int64_t max_age_us)
{
    if (now_us <= 0 || max_age_us <= 0) return;
    portENTER_CRITICAL(&s_mux);
    for (uint8_t i = 0; i < OBD_TPMS_WHEEL_COUNT; ++i) {
        if (s_snapshot.valid[i] && s_snapshot.updated_us[i] > 0 &&
            now_us >= s_snapshot.updated_us[i] &&
            now_us - s_snapshot.updated_us[i] > max_age_us) {
            s_snapshot.valid[i] = false;
        }
    }
    portEXIT_CRITICAL(&s_mux);
}

bool obd_tpms_cache_get(obd_tpms_wheel_t wheel, int16_t *pressure_bar_x100,
                        int64_t *updated_us)
{
    bool valid;
    if (!valid_wheel(wheel)) return false;
    portENTER_CRITICAL(&s_mux);
    valid = s_snapshot.valid[wheel];
    if (pressure_bar_x100) *pressure_bar_x100 = s_snapshot.pressure_bar_x100[wheel];
    if (updated_us) *updated_us = s_snapshot.updated_us[wheel];
    portEXIT_CRITICAL(&s_mux);
    return valid;
}

void obd_tpms_cache_get_snapshot(obd_tpms_snapshot_t *out)
{
    if (!out) return;
    portENTER_CRITICAL(&s_mux);
    *out = s_snapshot;
    portEXIT_CRITICAL(&s_mux);
}
