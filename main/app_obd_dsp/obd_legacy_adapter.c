#include "obd_legacy_adapter.h"

#include <string.h>

#include "vehicle_custom_config.h"

#define LEGACY_RULE_CAPACITY 24

static obd_data_rule_t s_rules[LEGACY_RULE_CAPACITY];
static obd_rule_pack_t s_pack;
static const vehicle_profile_t *s_profile;

static uint16_t make_rule_id(obd_rule_kind_t kind, uint32_t address, uint8_t index)
{
    // Keep IDs deterministic while leaving the low bits available for a
    // formula/CAN table entry that shares an address.
    return (uint16_t)((((uint16_t)kind << 13) |
                       (uint16_t)(address & 0x1FFFu)) ^ index);
}

static const char *formula_header(const vehicle_override_t *override)
{
    if (!override) return NULL;
    if (override->uds_header_cmd) return override->uds_header_cmd;
    return override->functional_addr ? "ATSH7E0\r" : NULL;
}

static const char *strategy_header(const vehicle_profile_t *profile,
                                   const vehicle_override_t *override,
                                   oil_temp_query_mode_t mode)
{
    if (override && override->uds_header_cmd) return override->uds_header_cmd;
    if (mode == OIL_TEMP_MODE_PID_5C || mode == OIL_TEMP_MODE_TOYOTA_21_01)
        return NULL;
    // Manufacturer-specific UDS requests on a functional-addressed profile
    // are sent to the engine ECU physically, matching the legacy path.
    if (profile && profile->obd_functional_addr) return "ATSH7E0\r";
    return NULL;
}

static bool convert_strategy(const vehicle_profile_t *profile,
                             const vehicle_override_t *override,
                             oil_temp_query_mode_t mode,
                             uint8_t fallback_rank,
                             obd_data_rule_t *out)
{
    uint16_t did = 0;
    uint8_t resp_byte = 0;
    uint8_t resp_bytes = 1;
    float scale = 1.0f;
    float offset = -40.0f;
    obd_protocol_id_t protocol = OBD_PROTOCOL_UDS22;
    obd_rule_kind_t kind = OBD_RULE_MODE22;

    if (!out || mode == OIL_TEMP_MODE_NONE) return false;
    switch (mode) {
        case OIL_TEMP_MODE_PID_5C:
            protocol = OBD_PROTOCOL_STANDARD;
            kind = OBD_RULE_MODE01;
            did = 0x5C;
            break;
        case OIL_TEMP_MODE_UDS_22_10_17:
            did = 0x1017;
            break;
        case OIL_TEMP_MODE_TOYOTA_21_01:
            protocol = OBD_PROTOCOL_MODE21;
            kind = OBD_RULE_MODE21;
            did = 0x01;
            break;
        case OIL_TEMP_MODE_MAZDA_22_111F:
            did = 0x111F; offset = -50.0f; break;
        case OIL_TEMP_MODE_MAZDA_22_1310:
            did = 0x1310; resp_bytes = 2; scale = 0.01f; break;
        case OIL_TEMP_MODE_MINI_22_5822:
            did = 0x5822; offset = -60.0f; break;
        case OIL_TEMP_MODE_BMW_22_4402:
            did = 0x4402; resp_byte = 1; offset = -64.0f; break;
        case OIL_TEMP_MODE_BMW_22_03F3:
            did = 0x03F3; break;
        case OIL_TEMP_MODE_BMW_G_22_4402:
            did = 0x4402; resp_bytes = 2; scale = 191.25f / 255.0f; offset = -48.0f; break;
        case OIL_TEMP_MODE_BMW_22_D002:
            did = 0xD002; resp_bytes = 2; scale = 191.25f / 255.0f; offset = -48.0f; break;
        case OIL_TEMP_MODE_BMW_22_111F:
            did = 0x111F; offset = -50.0f; break;
        default:
            return false;
    }

    memset(out, 0, sizeof(*out));
    out->protocol = protocol;
    out->kind = kind;
    out->service = (kind == OBD_RULE_MODE01) ? 0x01 :
                   (kind == OBD_RULE_MODE21 ? 0x21 : 0x22);
    out->address = did;
    out->header = strategy_header(profile, override, mode);
    out->channel = CH_OIL_TEMP;
    out->rule_id = make_rule_id(kind, did, (uint8_t)(0x80u + fallback_rank));
    out->resp_byte = resp_byte;
    out->resp_bytes = resp_bytes;
    out->big_endian = resp_bytes > 1;
    out->scale = scale;
    out->offset = offset;
    out->min_value = -40.0f;
    out->max_value = 215.0f;
    out->period_ms = 1000;
    out->source_priority = 20;
    out->schedule_slot = 6;
    out->fallback_rank = fallback_rank;
    return true;
}

static bool convert_formula(const vehicle_override_t *override,
                            const oil_formula_t *formula,
                            uint8_t index,
                            obd_data_rule_t *out)
{
    if (!formula || !out || formula->pid_len == 0) return false;

    memset(out, 0, sizeof(*out));
    out->protocol = (formula->type == OIL_SPECIAL) ? OBD_PROTOCOL_MODE21 :
                    (formula->type == OIL_UDS_22 ? OBD_PROTOCOL_UDS22 :
                                                    OBD_PROTOCOL_STANDARD);
    out->kind = (formula->type == OIL_SPECIAL) ? OBD_RULE_MODE21 :
                (formula->type == OIL_UDS_22 ? OBD_RULE_MODE22 :
                                                OBD_RULE_MODE01);
    out->service = (out->kind == OBD_RULE_MODE22) ? 0x22 :
                   (out->kind == OBD_RULE_MODE21 ? 0x21 : 0x01);
    out->address = (formula->pid_len >= 2) ?
                   (((uint32_t)formula->pid[0] << 8) | formula->pid[1]) :
                   formula->pid[0];
    // Header switching is only part of manufacturer-specific requests. A
    // standard PID or Mode 21 fallback uses the legacy default header.
    out->header = (out->kind == OBD_RULE_MODE22) ? formula_header(override) : NULL;
    out->channel = CH_OIL_TEMP;
    out->rule_id = make_rule_id(out->kind, out->address, index);
    out->resp_byte = formula->resp_byte;
    out->resp_bytes = formula->resp_bytes;
    out->big_endian = formula->resp_bytes > 1;
    out->scale = formula->scale;
    out->offset = formula->offset;
    out->min_value = -40.0f;
    out->max_value = 215.0f;
    out->period_ms = 1000;
    out->source_priority = 20;
    out->schedule_slot = 6;
    out->fallback_rank = (index == 0) ? 0 : 1;
    return true;
}

static bool convert_can_rule(const can_rule_t *legacy,
                             uint8_t index,
                             obd_data_rule_t *out)
{
    if (!legacy || !out || legacy->bit_len == 0) return false;
    memset(out, 0, sizeof(*out));
    out->protocol = OBD_PROTOCOL_CAN_MONITOR;
    out->kind = OBD_RULE_CAN;
    out->address = legacy->can_id;
    out->channel = legacy->channel;
    out->rule_id = make_rule_id(OBD_RULE_CAN, legacy->can_id, index);
    out->resp_byte = legacy->bit_off / 8;
    out->resp_bytes = (uint8_t)((legacy->bit_len + 7) / 8);
    out->bit_offset = legacy->bit_off;
    out->bit_length = legacy->bit_len;
    out->scale = legacy->scale;
    out->offset = legacy->offset;
    out->min_value = -100000.0f;
    out->max_value = 100000.0f;
    out->period_ms = 50;
    out->source_priority = 20;
    out->schedule_slot = 0xFF;
    return true;
}

static bool convert_profile_did(uint16_t did,
                                uint8_t channel,
                                const char *header,
                                uint8_t index,
                                obd_data_rule_t *out)
{
    if (!did || !out) return false;
    memset(out, 0, sizeof(*out));
    out->protocol = OBD_PROTOCOL_UDS22;
    out->kind = OBD_RULE_MODE22;
    out->service = 0x22;
    out->address = did;
    out->header = header;
    out->channel = channel;
    out->rule_id = make_rule_id(OBD_RULE_MODE22, did, index);
    out->resp_byte = 0;
    out->resp_bytes = (channel == CH_GEAR_RAW) ? 1 : 2;
    out->big_endian = true;
    out->scale = 1.0f;
    out->min_value = 0.0f;
    out->max_value = (channel == CH_OIL_PRESSURE_HPA) ? 20000.0f : 255.0f;
    out->period_ms = 1000;
    out->source_priority = 20;
    out->schedule_slot = (channel == CH_OIL_PRESSURE_HPA) ? 10 : 11;
    return true;
}

const obd_rule_pack_t *obd_legacy_rules_get(const vehicle_profile_t *profile)
{
    const vehicle_override_t *override;
    uint8_t count = 0;

    if (profile == s_profile) return s_pack.rule_count ? &s_pack : NULL;
    s_profile = profile;
    memset(s_rules, 0, sizeof(s_rules));
    memset(&s_pack, 0, sizeof(s_pack));
    if (!profile) return NULL;

    override = vehicle_profile_get_override();
    if (override && override->oil_primary) {
        if (convert_formula(override, override->oil_primary, 0, &s_rules[count])) ++count;
        if (count < LEGACY_RULE_CAPACITY &&
            convert_formula(override, override->oil_secondary, 1, &s_rules[count])) ++count;
    } else {
        const oil_temp_strategy_t *strategy = &profile->oil_temp_strategy;
        const oil_temp_query_mode_t modes[] = {
            strategy->primary, strategy->secondary,
            strategy->tertiary, strategy->quaternary,
        };
        for (uint8_t rank = 0; rank < 4 && count < LEGACY_RULE_CAPACITY; ++rank) {
            if (convert_strategy(profile, override, modes[rank], rank, &s_rules[count])) ++count;
        }
    }

    if (override) {
        for (uint8_t i = 0; i < override->can_rule_count && count < LEGACY_RULE_CAPACITY; ++i) {
            if (convert_can_rule(&override->can_rules[i], count, &s_rules[count])) ++count;
        }
    }

    // These profile-level DIDs were historically handled by special branches
    // in the BLE callback. They now have stable channels while their legacy
    // request preparation remains in the compatibility scheduler.
    const char *oil_header = override ? override->uds_header_cmd : NULL;
    if (count < LEGACY_RULE_CAPACITY &&
        convert_profile_did(profile->obd_oil_pressure_did, CH_OIL_PRESSURE_HPA,
                            oil_header, count, &s_rules[count])) {
        ++count;
    }
    const char *gear_header = override && override->obd_gear_header_cmd
                                  ? override->obd_gear_header_cmd
                                  : (override ? override->uds_header_cmd : NULL);
    if (count < LEGACY_RULE_CAPACITY &&
        convert_profile_did(profile->obd_gear_did, CH_GEAR_RAW,
                            gear_header, count, &s_rules[count])) {
        ++count;
    }
    if (count == 0) return NULL;

    s_pack.name = "legacy vehicle override adapter";
    s_pack.rules = s_rules;
    s_pack.rule_count = count;
    s_pack.priority = 20;
    return &s_pack;
}
