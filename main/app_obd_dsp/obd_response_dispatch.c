#include "obd_response_dispatch.h"

#include <ctype.h>
#include <string.h>

static bool header_matches(const char *rule_header, const char *response_header)
{
    // A NULL response header means the transport did not expose request
    // context. This compatibility mode still allows a unique DID/PID rule;
    // callers that have context pass the exact header to disambiguate rules.
    if (!response_header) return true;
    if (rule_header == response_header) return true;
    if (!rule_header || !response_header) return false;
    return strcmp(rule_header, response_header) == 0;
}

static const obd_data_rule_t *find_rule(const obd_response_dispatcher_t *dispatcher,
                                        obd_protocol_id_t protocol,
                                        obd_rule_kind_t kind,
                                        uint8_t service,
                                        uint32_t address,
                                        const char *header)
{
    const obd_composed_plan_t *plan = dispatcher ? dispatcher->plan : NULL;
    const obd_data_rule_t *wildcard = NULL;
    uint8_t wildcard_count = 0;
    if (!plan) return NULL;

    for (uint8_t i = 0; i < plan->rule_count; ++i) {
        const obd_composed_rule_t *candidate = &plan->rules[i];
        const obd_data_rule_t *rule = candidate->rule;
        if (candidate->disabled || !rule || rule->protocol != protocol ||
            rule->kind != kind || rule->service != service ||
            rule->address != address || !header_matches(rule->header, header)) {
            continue;
        }
        // Prefer an exact header match over a compatibility wildcard when
        // multiple regional/ECU rules share the same DID.
        if (header && rule->header && strcmp(rule->header, header) == 0) return rule;
        if (!header || !rule->header) {
            wildcard = rule;
            ++wildcard_count;
        }
    }
    // Without request context, a NULL-header rule is only safe when it is
    // the sole candidate. Returning the first match would silently decode a
    // response using the wrong regional/ECU rule.
    return wildcard_count == 1 ? wildcard : NULL;
}

static bool decode_bytes(const obd_data_rule_t *rule,
                         const uint8_t *payload,
                         size_t payload_len,
                         float *value)
{
    uint32_t raw = 0;
    if (!rule || !payload || !value || rule->resp_bytes == 0 ||
        rule->resp_bytes > 4 || (size_t)rule->resp_byte + rule->resp_bytes > payload_len) {
        return false;
    }

    if (rule->big_endian) {
        for (uint8_t i = 0; i < rule->resp_bytes; ++i)
            raw = (raw << 8) | payload[rule->resp_byte + i];
    } else {
        for (uint8_t i = 0; i < rule->resp_bytes; ++i)
            raw |= (uint32_t)payload[rule->resp_byte + i] << (8u * i);
    }

    *value = (float)raw * rule->scale + rule->offset;
    return *value >= rule->min_value && *value <= rule->max_value;
}

static bool decode_can_bits(const obd_data_rule_t *rule,
                            const uint8_t *payload,
                            size_t payload_len,
                            float *value)
{
    uint32_t raw = 0;
    if (!rule || !payload || !value || rule->bit_length == 0 ||
        rule->bit_length > 32 ||
        (uint16_t)rule->bit_offset + rule->bit_length > payload_len * 8u) {
        return false;
    }
    for (uint8_t i = 0; i < rule->bit_length; ++i) {
        uint16_t bit = (uint16_t)rule->bit_offset + i;
        if (payload[bit / 8u] & (uint8_t)(1u << (bit % 8u))) raw |= 1u << i;
    }
    *value = (float)raw * rule->scale + rule->offset;
    return *value >= rule->min_value && *value <= rule->max_value;
}

static bool parse_hex_token(const char **cursor, uint32_t *value)
{
    const char *p = cursor ? *cursor : NULL;
    uint32_t parsed = 0;
    bool saw_digit = false;
    if (!p || !value) return false;
    while (*p && isxdigit((unsigned char)*p)) {
        uint8_t nibble;
        if (*p >= '0' && *p <= '9') nibble = (uint8_t)(*p - '0');
        else if (*p >= 'A' && *p <= 'F') nibble = (uint8_t)(*p - 'A' + 10);
        else nibble = (uint8_t)(*p - 'a' + 10);
        parsed = (parsed << 4) | nibble;
        saw_digit = true;
        ++p;
    }
    if (!saw_digit) return false;
    *cursor = p;
    *value = parsed;
    return true;
}

static bool find_service_frame(const char *response,
                               uint8_t service,
                               uint32_t *tokens,
                               size_t max_tokens,
                               size_t *token_count)
{
    const char *p = response;
    if (!response || !tokens || max_tokens == 0 || !token_count) return false;
    while (*p) {
        while (*p && !isxdigit((unsigned char)*p)) ++p;
        if (!*p) break;
        const char *token_start = p;
        uint32_t token = 0;
        if (!parse_hex_token(&p, &token)) {
            p = token_start + 1;
            continue;
        }
        // ELM CAN IDs and ISO-TP line labels are wider than one byte. Only a
        // byte-sized token can be a normalized service marker.
        if (token != service || token > 0xFFu) continue;

        size_t count = 0;
        tokens[count++] = token;
        while (*p && count < max_tokens) {
            while (*p && !isxdigit((unsigned char)*p)) {
                if (*p == '>') break;
                ++p;
            }
            if (!*p || *p == '>') break;
            uint32_t next = 0;
            const char *next_start = p;
            if (!parse_hex_token(&p, &next)) {
                p = next_start + 1;
                continue;
            }
            if (next <= 0xFFu) tokens[count++] = next;
        }
        *token_count = count;
        return true;
    }
    return false;
}

static bool dispatch_value(const obd_response_dispatcher_t *dispatcher,
                           const obd_data_rule_t *rule,
                           float value,
                           obd_response_service_t service)
{
    if (!dispatcher || !rule || !dispatcher->sink || !dispatcher->sink->on_value)
        return false;
    dispatcher->sink->on_value(dispatcher->ctx, rule, value);
    if (dispatcher->sink->on_frame_valid)
        dispatcher->sink->on_frame_valid(dispatcher->ctx, service);
    return true;
}

bool obd_response_dispatch_mode01(const obd_response_dispatcher_t *dispatcher,
                                  uint8_t pid,
                                  const uint8_t *payload,
                                  size_t payload_len,
                                  const char *header)
{
    const obd_data_rule_t *rule = find_rule(dispatcher, OBD_PROTOCOL_STANDARD,
                                            OBD_RULE_MODE01, 0x01, pid, header);
    float value;
    if (!rule || !decode_bytes(rule, payload, payload_len, &value)) return false;
    return dispatch_value(dispatcher, rule, value, OBD_RESPONSE_MODE01);
}

bool obd_response_dispatch_mode22(const obd_response_dispatcher_t *dispatcher,
                                  uint16_t did,
                                  const uint8_t *payload,
                                  size_t payload_len,
                                  const char *header)
{
    const obd_data_rule_t *rule = find_rule(dispatcher, OBD_PROTOCOL_UDS22,
                                            OBD_RULE_MODE22, 0x22, did, header);
    float value;
    if (!rule || !decode_bytes(rule, payload, payload_len, &value)) return false;
    return dispatch_value(dispatcher, rule, value, OBD_RESPONSE_MODE22);
}

bool obd_response_dispatch_can(const obd_response_dispatcher_t *dispatcher,
                               uint32_t can_id,
                               const uint8_t *payload,
                               size_t payload_len,
                               const char *header)
{
    bool dispatched = false;
    const obd_composed_plan_t *plan = dispatcher ? dispatcher->plan : NULL;
    if (!plan || !payload) return false;

    for (uint8_t i = 0; i < plan->rule_count; ++i) {
        const obd_composed_rule_t *candidate = &plan->rules[i];
        const obd_data_rule_t *rule = candidate->rule;
        float value;
        if (candidate->disabled || !rule || rule->protocol != OBD_PROTOCOL_CAN_MONITOR ||
            rule->kind != OBD_RULE_CAN || rule->service != 0 ||
            rule->address != can_id || !header_matches(rule->header, header) ||
            !decode_can_bits(rule, payload, payload_len, &value)) {
            continue;
        }
        dispatched |= dispatch_value(dispatcher, rule, value, OBD_RESPONSE_MODE01);
    }
    return dispatched;
}

bool obd_response_dispatch_text_mode01(const obd_text_response_context_t *context,
                                       const char *response)
{
    uint32_t tokens[16] = {0};
    size_t count = 0;
    if (!context || !context->dispatcher ||
        !find_service_frame(response, OBD_RESPONSE_MODE01, tokens,
                             sizeof(tokens) / sizeof(tokens[0]), &count) ||
        count < 3) {
        return false;
    }
    uint8_t payload[14] = {0};
    size_t payload_len = count - 2;
    if (payload_len > sizeof(payload)) payload_len = sizeof(payload);
    for (size_t i = 0; i < payload_len; ++i)
        payload[i] = (uint8_t)tokens[i + 2];
    return obd_response_dispatch_mode01(context->dispatcher,
                                        (uint8_t)tokens[1], payload,
                                        payload_len, context->header);
}

bool obd_response_dispatch_text_mode22(const obd_text_response_context_t *context,
                                       const char *response)
{
    uint32_t tokens[16] = {0};
    size_t count = 0;
    if (!context || !context->dispatcher ||
        !find_service_frame(response, OBD_RESPONSE_MODE22, tokens,
                             sizeof(tokens) / sizeof(tokens[0]), &count) ||
        count < 4) {
        return false;
    }
    uint8_t payload[13] = {0};
    size_t payload_len = count - 3;
    if (payload_len > sizeof(payload)) payload_len = sizeof(payload);
    for (size_t i = 0; i < payload_len; ++i)
        payload[i] = (uint8_t)tokens[i + 3];
    return obd_response_dispatch_mode22(
        context->dispatcher,
        (uint16_t)((tokens[1] << 8) | tokens[2]), payload, payload_len,
        context->header);
}

static bool parse_can_frame_text(const char *response,
                                 uint32_t *can_id,
                                 uint8_t *payload,
                                 size_t *payload_len)
{
    const char *p = response;
    uint32_t tokens[12] = {0};
    size_t count = 0;
    if (!response || !can_id || !payload || !payload_len) return false;

    // Tokenize one monitor line. A channel prefix such as "0:" is handled
    // below; punctuation and whitespace are separators for clone adapters.
    while (*p && count < (sizeof(tokens) / sizeof(tokens[0]))) {
        while (*p && !isxdigit((unsigned char)*p)) {
            if (*p == '>' || *p == '\r' || *p == '\n') break;
            ++p;
        }
        if (!*p || *p == '>' || *p == '\r' || *p == '\n') break;
        const char *start = p;
        uint32_t token = 0;
        if (!parse_hex_token(&p, &token)) {
            p = start + 1;
            continue;
        }
        tokens[count++] = token;
    }
    if (count < 2) return false;

    size_t id_index = 0;
    // ELM multi-channel output commonly prefixes frames with "0:". Only
    // treat a token as a channel when the colon is immediately after it.
    const char *after_first = response;
    while (*after_first && isspace((unsigned char)*after_first)) ++after_first;
    const char *first_token = after_first;
    while (*after_first && isxdigit((unsigned char)*after_first)) ++after_first;
    if (after_first > first_token && *after_first == ':' &&
        tokens[0] <= 0xFFu) {
        id_index = 1;
    }
    if (id_index >= count || tokens[id_index] > 0x1FFFFFFFu) return false;
    *can_id = tokens[id_index];

    size_t first_data = id_index + 1;
    size_t data_count = count - first_data;
    if (data_count == 0) return false;

    // Some adapters include a DLC byte after the CAN ID. Accept it only when
    // it exactly matches the number of bytes that follow, avoiding accidental
    // stripping of ordinary payload bytes in the common no-DLC form.
    if (data_count >= 2 && tokens[first_data] <= 8u &&
        tokens[first_data] == data_count - 1u) {
        ++first_data;
        --data_count;
    }
    if (data_count == 0 || data_count > 8) return false;
    for (size_t i = 0; i < data_count; ++i) {
        if (tokens[first_data + i] > 0xFFu) return false;
        payload[i] = (uint8_t)tokens[first_data + i];
    }
    *payload_len = data_count;
    return true;
}

bool obd_response_dispatch_text_can(const obd_text_response_context_t *context,
                                    const char *response)
{
    uint32_t can_id = 0;
    uint8_t payload[8] = {0};
    size_t payload_len = 0;
    if (!context || !context->dispatcher ||
        !parse_can_frame_text(response, &can_id, payload, &payload_len)) {
        return false;
    }
    return obd_response_dispatch_can(context->dispatcher, can_id, payload,
                                     payload_len, context->header);
}
