#include "ford_mondeo_tpms.h"

#include "vehicle_custom_config.h"

#define FORD_MONDEO_2014_TPMS_HEADER "ATSH726\r"
#define FORD_MONDEO_2014_TPMS_SCALE (0.05f * 0.0689475729f)

// Validated on a 2014 Mondeo 2.0T using five vLinker/FORScan capture sessions:
// BCMii request ID 0x726, response ID 0x72E, 2813=FL, 2814=FR, 2816=RL,
// 2815=RR. The two data bytes are a big-endian value in 0.05 psi units.
static const obd_data_rule_t s_rules[] = {
    { .rule_id = 0xF813, .protocol = OBD_PROTOCOL_UDS22, .kind = OBD_RULE_MODE22,
      .service = 0x22, .address = 0x2813, .header = FORD_MONDEO_2014_TPMS_HEADER,
      .channel = CH_TPMS_FL_BAR_X100, .resp_byte = 0, .resp_bytes = 2,
      .big_endian = true, .scale = FORD_MONDEO_2014_TPMS_SCALE, .min_value = 0.0f,
      .max_value = 20.0f, .period_ms = 1200, .source_priority = 30,
      .schedule_slot = 12, .fallback_rank = 0 },
    { .rule_id = 0xF814, .protocol = OBD_PROTOCOL_UDS22, .kind = OBD_RULE_MODE22,
      .service = 0x22, .address = 0x2814, .header = FORD_MONDEO_2014_TPMS_HEADER,
      .channel = CH_TPMS_FR_BAR_X100, .resp_byte = 0, .resp_bytes = 2,
      .big_endian = true, .scale = FORD_MONDEO_2014_TPMS_SCALE, .min_value = 0.0f,
      .max_value = 20.0f, .period_ms = 1200, .source_priority = 30,
      .schedule_slot = 13, .fallback_rank = 0 },
    { .rule_id = 0xF816, .protocol = OBD_PROTOCOL_UDS22, .kind = OBD_RULE_MODE22,
      .service = 0x22, .address = 0x2816, .header = FORD_MONDEO_2014_TPMS_HEADER,
      .channel = CH_TPMS_RL_BAR_X100, .resp_byte = 0, .resp_bytes = 2,
      .big_endian = true, .scale = FORD_MONDEO_2014_TPMS_SCALE, .min_value = 0.0f,
      .max_value = 20.0f, .period_ms = 1200, .source_priority = 30,
      .schedule_slot = 14, .fallback_rank = 0 },
    { .rule_id = 0xF815, .protocol = OBD_PROTOCOL_UDS22, .kind = OBD_RULE_MODE22,
      .service = 0x22, .address = 0x2815, .header = FORD_MONDEO_2014_TPMS_HEADER,
      .channel = CH_TPMS_RR_BAR_X100, .resp_byte = 0, .resp_bytes = 2,
      .big_endian = true, .scale = FORD_MONDEO_2014_TPMS_SCALE, .min_value = 0.0f,
      .max_value = 20.0f, .period_ms = 1200, .source_priority = 30,
      .schedule_slot = 15, .fallback_rank = 0 },
};

static const obd_rule_pack_t s_pack = {
    .name = "ford_mondeo_2014_tpms",
    .rules = s_rules,
    .rule_count = (uint16_t)(sizeof(s_rules) / sizeof(s_rules[0])),
    .priority = 30,
};

const obd_rule_pack_t *obd_ford_mondeo_2014_tpms_rules_get(void)
{
    return &s_pack;
}
