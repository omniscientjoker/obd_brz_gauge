#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "obd_protocol_types.h"
#include "vehicle_custom_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float value;
    uint16_t source_rule_id;
    uint8_t source_priority;
    uint32_t freshness_ms;
    int64_t updated_us;
    bool valid;
} obd_channel_value_t;

typedef struct {
    obd_channel_value_t values[CH_COUNT];
} obd_channel_arbiter_t;

void obd_channel_arbiter_reset(obd_channel_arbiter_t *arbiter);

// Publish only when the source wins priority or the current source is stale.
// Returns true when the caller should forward the value to the data cache.
bool obd_channel_arbiter_publish(obd_channel_arbiter_t *arbiter,
                                 const obd_data_rule_t *rule,
                                 float value,
                                 int64_t now_us);

void obd_channel_arbiter_invalidate(obd_channel_arbiter_t *arbiter,
                                    uint8_t channel);

const obd_channel_value_t *obd_channel_arbiter_get(
    const obd_channel_arbiter_t *arbiter, uint8_t channel);

#ifdef __cplusplus
}
#endif
