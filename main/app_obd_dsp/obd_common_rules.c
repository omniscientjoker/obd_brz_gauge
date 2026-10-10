#include "obd_common_rules.h"
#include "vehicle_custom_config.h"

// Keep these IDs stable. They are used as composition keys when a vehicle
// pack replaces one standard rule with a vehicle-specific source.
static const obd_data_rule_t s_common_rules[] = {
    { .rule_id = 0x010C, .protocol = OBD_PROTOCOL_STANDARD, .kind = OBD_RULE_MODE01,
      .service = 0x01, .address = 0x0C, .channel = CH_RPM, .resp_byte = 0,
      .resp_bytes = 2, .big_endian = true, .scale = 0.25f,
      .min_value = 0.0f, .max_value = 16383.0f, .period_ms = 250, .schedule_slot = 0 },
    { .rule_id = 0x010D, .protocol = OBD_PROTOCOL_STANDARD, .kind = OBD_RULE_MODE01,
      .service = 0x01, .address = 0x0D, .channel = CH_SPEED, .resp_byte = 0,
      .resp_bytes = 1, .big_endian = false, .scale = 1.0f,
      .min_value = 0.0f, .max_value = 255.0f, .period_ms = 250, .schedule_slot = 2 },
    { .rule_id = 0x0104, .protocol = OBD_PROTOCOL_STANDARD, .kind = OBD_RULE_MODE01,
      .service = 0x01, .address = 0x04, .channel = CH_LOAD, .resp_byte = 0,
      .resp_bytes = 1, .big_endian = false, .scale = 100.0f / 255.0f,
      .min_value = 0.0f, .max_value = 100.0f, .period_ms = 500, .schedule_slot = 4 },
    { .rule_id = 0x0105, .protocol = OBD_PROTOCOL_STANDARD, .kind = OBD_RULE_MODE01,
      .service = 0x01, .address = 0x05, .channel = CH_COOLANT, .resp_byte = 0,
      .resp_bytes = 1, .big_endian = false, .scale = 1.0f, .offset = -40.0f,
      .min_value = -40.0f, .max_value = 215.0f, .period_ms = 500, .schedule_slot = 3 },
    { .rule_id = 0x010B, .protocol = OBD_PROTOCOL_STANDARD, .kind = OBD_RULE_MODE01,
      .service = 0x01, .address = 0x0B, .channel = CH_MAP_KPA, .resp_byte = 0,
      .resp_bytes = 1, .big_endian = false, .scale = 1.0f,
      .min_value = 0.0f, .max_value = 255.0f, .period_ms = 500, .schedule_slot = 8 },
    { .rule_id = 0x010F, .protocol = OBD_PROTOCOL_STANDARD, .kind = OBD_RULE_MODE01,
      .service = 0x01, .address = 0x0F, .channel = CH_INTAKE, .resp_byte = 0,
      .resp_bytes = 1, .big_endian = false, .scale = 1.0f, .offset = -40.0f,
      .min_value = -40.0f, .max_value = 215.0f, .period_ms = 500, .schedule_slot = 1 },
    { .rule_id = 0x0111, .protocol = OBD_PROTOCOL_STANDARD, .kind = OBD_RULE_MODE01,
      .service = 0x01, .address = 0x11, .channel = CH_TPS, .resp_byte = 0,
      .resp_bytes = 1, .big_endian = false, .scale = 100.0f / 255.0f,
      .min_value = 0.0f, .max_value = 100.0f, .period_ms = 500, .schedule_slot = 5 },
    { .rule_id = 0x0142, .protocol = OBD_PROTOCOL_STANDARD, .kind = OBD_RULE_MODE01,
      .service = 0x01, .address = 0x42, .channel = CH_BAT_MV, .resp_byte = 0,
      .resp_bytes = 2, .big_endian = true, .scale = 1.0f,
      .min_value = 0.0f, .max_value = 65535.0f, .period_ms = 1000, .schedule_slot = 7 },
    { .rule_id = 0x012F, .protocol = OBD_PROTOCOL_STANDARD, .kind = OBD_RULE_MODE01,
      .service = 0x01, .address = 0x2F, .channel = CH_FUEL_PCT, .resp_byte = 0,
      .resp_bytes = 1, .big_endian = false, .scale = 100.0f / 255.0f,
      .min_value = 0.0f, .max_value = 100.0f, .period_ms = 2000, .schedule_slot = 12 },
    { .rule_id = 0x0144, .protocol = OBD_PROTOCOL_STANDARD, .kind = OBD_RULE_MODE01,
      .service = 0x01, .address = 0x44, .channel = CH_AFR_X100, .resp_byte = 0,
      .resp_bytes = 2, .big_endian = true, .scale = 1470.0f / 32768.0f,
      .min_value = 0.0f, .max_value = 2200.0f, .period_ms = 1000, .schedule_slot = 9 },
    { .rule_id = 0x015C, .protocol = OBD_PROTOCOL_STANDARD, .kind = OBD_RULE_MODE01,
      .service = 0x01, .address = 0x5C, .channel = CH_OIL_TEMP, .resp_byte = 0,
      .resp_bytes = 1, .big_endian = false, .scale = 1.0f, .offset = -40.0f,
      .min_value = -40.0f, .max_value = 215.0f, .period_ms = 1000, .schedule_slot = 6 },
};

static const obd_rule_pack_t s_common_pack = {
    .name = "SAE J1979 common",
    .rules = s_common_rules,
    .rule_count = (uint16_t)(sizeof(s_common_rules) / sizeof(s_common_rules[0])),
    .priority = 0,
};

const obd_rule_pack_t *obd_common_rules_get(void)
{
    return &s_common_pack;
}
