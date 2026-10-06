#include "obd_channel_arbiter.h"

#include <string.h>

void obd_channel_arbiter_reset(obd_channel_arbiter_t *arbiter)
{
    if (arbiter) memset(arbiter, 0, sizeof(*arbiter));
}

bool obd_channel_arbiter_publish(obd_channel_arbiter_t *arbiter,
                                 const obd_data_rule_t *rule,
                                 float value,
                                 int64_t now_us)
{
    obd_channel_value_t *current;
    uint32_t freshness_ms;
    bool stale;

    if (!arbiter || !rule || rule->channel >= CH_COUNT) return false;
    current = &arbiter->values[rule->channel];
    freshness_ms = rule->period_ms ? rule->period_ms * 3u : 3000u;
    stale = !current->valid || current->updated_us <= 0 || now_us < current->updated_us ||
            now_us - current->updated_us > (int64_t)freshness_ms * 1000;
    if (!stale && current->source_priority > rule->source_priority) return false;
    if (!stale && current->source_priority == rule->source_priority &&
        current->source_rule_id == rule->rule_id && now_us < current->updated_us) return false;

    *current = (obd_channel_value_t){
        .value = value,
        .source_rule_id = rule->rule_id,
        .source_priority = rule->source_priority,
        .freshness_ms = freshness_ms,
        .updated_us = now_us,
        .valid = true,
    };
    return true;
}

void obd_channel_arbiter_invalidate(obd_channel_arbiter_t *arbiter,
                                    uint8_t channel)
{
    if (arbiter && channel < CH_COUNT) arbiter->values[channel].valid = false;
}

const obd_channel_value_t *obd_channel_arbiter_get(
    const obd_channel_arbiter_t *arbiter, uint8_t channel)
{
    if (!arbiter || channel >= CH_COUNT) return NULL;
    return &arbiter->values[channel];
}
