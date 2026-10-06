#include "obd_protocol_registry.h"

#include <stdio.h>

#include "obd_response_dispatch.h"

static bool parse_standard_response(void *ctx, const char *response)
{
    return obd_response_dispatch_text_mode01(
        (const obd_text_response_context_t *)ctx, response);
}

static bool parse_uds22_response(void *ctx, const char *response)
{
    return obd_response_dispatch_text_mode22(
        (const obd_text_response_context_t *)ctx, response);
}

static bool parse_can_response(void *ctx, const char *response)
{
    return obd_response_dispatch_text_can(
        (const obd_text_response_context_t *)ctx, response);
}

static bool build_standard_request(const obd_data_rule_t *rule, char *out, size_t out_len)
{
    return rule && out && out_len > 0 &&
           snprintf(out, out_len, "%02X %02X\r", rule->service & 0xFFu,
                    rule->address & 0xFFu) > 0;
}

static bool build_mode22_request(const obd_data_rule_t *rule, char *out, size_t out_len)
{
    return rule && out && out_len > 0 &&
           snprintf(out, out_len, "%02X %02X %02X\r", rule->service & 0xFFu,
                    (rule->address >> 8) & 0xFFu, rule->address & 0xFFu) > 0;
}

static bool build_mode21_request(const obd_data_rule_t *rule, char *out, size_t out_len)
{
    return rule && out && out_len > 0 &&
           snprintf(out, out_len, "%02X %02X\r", rule->service & 0xFFu,
                    rule->address & 0xFFu) > 0;
}

static const obd_protocol_module_t s_protocols[OBD_PROTOCOL_COUNT] = {
    [OBD_PROTOCOL_STANDARD] = {
        .id = OBD_PROTOCOL_STANDARD,
        .name = "OBD-II standard",
        .build_request = build_standard_request,
        .parse_response = parse_standard_response,
    },
    [OBD_PROTOCOL_UDS22] = {
        .id = OBD_PROTOCOL_UDS22,
        .name = "UDS Mode 22",
        .build_request = build_mode22_request,
        .parse_response = parse_uds22_response,
    },
    [OBD_PROTOCOL_CAN_MONITOR] = {
        .id = OBD_PROTOCOL_CAN_MONITOR,
        .name = "CAN monitor",
        .parse_response = parse_can_response,
    },
    [OBD_PROTOCOL_MODE21] = {
        .id = OBD_PROTOCOL_MODE21,
        .name = "Mode 21 special",
        .build_request = build_mode21_request,
    },
};

const obd_protocol_module_t *obd_protocol_get(obd_protocol_id_t id)
{
    if (id >= OBD_PROTOCOL_COUNT) return NULL;
    return &s_protocols[id];
}

bool obd_protocol_build_request(const obd_protocol_module_t *module,
                                const obd_data_rule_t *rule,
                                char *out,
                                size_t out_len)
{
    return module && module->build_request &&
           module->build_request(rule, out, out_len);
}
