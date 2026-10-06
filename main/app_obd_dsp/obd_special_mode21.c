#include "obd_special_mode21.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define MODE21_MAX_DATA 64
#define MODE21_TAIL_OFFSET 5

static bool parse_hex_byte(const char **cursor, uint8_t *value_out)
{
    const char *p = cursor ? *cursor : NULL;
    uint32_t value = 0;
    bool saw_digit = false;
    if (!p || !value_out) return false;
    while (*p && isxdigit((unsigned char)*p)) {
        uint8_t nibble;
        if (*p >= '0' && *p <= '9') nibble = (uint8_t)(*p - '0');
        else if (*p >= 'A' && *p <= 'F') nibble = (uint8_t)(*p - 'A' + 10);
        else nibble = (uint8_t)(*p - 'a' + 10);
        value = (value << 4) | nibble;
        saw_digit = true;
        ++p;
    }
    if (!saw_digit || value > 0xFFu) return false;
    *cursor = p;
    *value_out = (uint8_t)value;
    return true;
}

static int parse_data_bytes(const char *response, uint8_t *out, int max_out)
{
    const char *p = response ? strstr(response, "61 01") : NULL;
    if (!p || !out || max_out <= 0) return 0;
    p += 5;
    if (*p == ' ') ++p;

    int count = 0;
    bool new_line = false;
    while (*p && count < max_out) {
        if (*p == '>') break;
        if (*p == '\r' || *p == '\n') {
            new_line = true;
            ++p;
            continue;
        }
        if (new_line) {
            while (isdigit((unsigned char)*p)) ++p;
            if (*p == ':') ++p;
            while (*p == ' ') ++p;
            // ISO-TP consecutive-frame lines begin with 0x20..0x2F. The
            // marker is transport metadata, not a sensor byte.
            const char *peek = p;
            uint8_t marker = 0;
            if (parse_hex_byte(&peek, &marker) && marker >= 0x20 && marker <= 0x2F) {
                p = peek;
                if (*p == ' ') ++p;
            }
            new_line = false;
            continue;
        }
        const char *peek = p;
        uint8_t value = 0;
        if (parse_hex_byte(&peek, &value)) {
            out[count++] = value;
            p = peek;
        } else {
            ++p;
        }
        if (*p == ' ') ++p;
    }
    return count;
}

void obd_mode21_reset(obd_mode21_state_t *state, bool zc_profile)
{
    if (!state) return;
    *state = (obd_mode21_state_t){
        .adaptive_oil_index = 33,
        .last_oil_c = -100,
        .hold_count = 0,
        .last_accepted_us = 0,
        .zc_profile = zc_profile,
    };
}

static bool accept_consistent(obd_mode21_state_t *state, int32_t value,
                              int64_t now_us, bool use_time_gap)
{
    if (!state) return false;
    bool time_gap = !use_time_gap || state->last_accepted_us == 0 ||
                    now_us - state->last_accepted_us > 3000000;
    bool consistent = state->last_oil_c <= -50 || time_gap ||
                      abs((int)value - (int)state->last_oil_c) <= 8;
    if (consistent) {
        state->last_oil_c = (int16_t)value;
        if (use_time_gap) state->last_accepted_us = now_us;
        state->hold_count = 0;
        return true;
    }
    if (state->hold_count < 30) {
        ++state->hold_count;
        return true;
    }
    state->last_oil_c = (int16_t)value;
    if (use_time_gap) state->last_accepted_us = now_us;
    state->hold_count = 0;
    return true;
}

bool obd_mode21_parse_response(obd_mode21_state_t *state,
                               const char *response,
                               int16_t coolant_temp,
                               int64_t now_us,
                               int32_t *oil_temp_c)
{
    uint8_t data[MODE21_MAX_DATA] = {0};
    int count;
    if (!state || !response || !oil_temp_c) return false;
    count = parse_data_bytes(response, data, MODE21_MAX_DATA);
    if (count <= 0) return false;

    if (state->zc_profile) {
        int index = count - MODE21_TAIL_OFFSET;
        if (index < 0 || index >= count) return false;
        int32_t value = (int32_t)data[index] - 40;
        if (value < -10 || value > 150) return false;
        // ZN/C6 responses occasionally differ by one byte. Use the tail
        // position and retain the last stable value during short glitches.
        if (!accept_consistent(state, value, now_us, true)) return false;
        *oil_temp_c = state->last_oil_c;
        return true;
    }

    if (state->adaptive_oil_index >= 0 && state->adaptive_oil_index < count) {
        int32_t value = (int32_t)data[state->adaptive_oil_index] - 40;
        if (value >= -10 && value <= 150) {
            if (accept_consistent(state, value, now_us, false)) {
                *oil_temp_c = state->last_oil_c;
                return true;
            }
        }
    }

    int best_index = -1;
    int32_t best_value = 0;
    int best_score = -1;
    for (int index = 0; index < count; ++index) {
        int32_t value = (int32_t)data[index] - 40;
        if (value < -10 || value > 150) continue;
        if (coolant_temp > -40) {
            int diff = (int)value - coolant_temp;
            if (diff < -25 || diff > 25) continue;
            int priority = (diff > 0 && diff <= 20) ? 1000 :
                           (diff >= -5 && diff <= 0) ? 700 :
                           (diff > 20 && diff <= 25) ? 400 : 100;
            int score = priority - abs(diff);
            if (score > best_score) {
                best_score = score;
                best_index = index;
                best_value = value;
            }
        } else if (best_index < 0) {
            best_index = index;
            best_value = value;
        }
    }
    if (best_index < 0) return false;
    state->adaptive_oil_index = best_index;
    state->last_oil_c = (int16_t)best_value;
    state->hold_count = 0;
    *oil_temp_c = best_value;
    return true;
}

static void mode21_handler_reset(void *ctx)
{
    obd_mode21_handler_context_t *context =
        (obd_mode21_handler_context_t *)ctx;
    if (!context) return;
    // Preserve the profile-specific tail-byte mode when a transport reset is
    // requested by a future handler dispatcher.
    obd_mode21_reset(&context->state, context->state.zc_profile);
    context->coolant_temp = -40;
    context->now_us = 0;
    context->oil_temp_c = 0;
    context->value_valid = false;
}

static bool mode21_handler_consume(void *ctx, const char *response)
{
    obd_mode21_handler_context_t *context =
        (obd_mode21_handler_context_t *)ctx;
    if (!context) return false;
    context->value_valid = obd_mode21_parse_response(
        &context->state, response, context->coolant_temp, context->now_us,
        &context->oil_temp_c);
    return context->value_valid;
}

static const obd_special_handler_t s_mode21_handler = {
    .handler_id = OBD_SPECIAL_HANDLER_MODE21,
    .name = "Toyota/Subaru Mode 21 oil temperature",
    .reset = mode21_handler_reset,
    .consume_response = mode21_handler_consume,
};

const obd_special_handler_t *obd_mode21_handler_get(void)
{
    return &s_mode21_handler;
}

void obd_mode21_handler_init(obd_mode21_handler_context_t *context,
                             bool zc_profile)
{
    if (!context) return;
    memset(context, 0, sizeof(*context));
    context->state.zc_profile = zc_profile;
    mode21_handler_reset(context);
}

void obd_mode21_handler_begin(obd_mode21_handler_context_t *context,
                              int16_t coolant_temp,
                              int64_t now_us)
{
    if (!context) return;
    context->coolant_temp = coolant_temp;
    context->now_us = now_us;
    context->oil_temp_c = 0;
    context->value_valid = false;
}

bool obd_mode21_handler_get_value(const obd_mode21_handler_context_t *context,
                                  int32_t *oil_temp_c)
{
    if (!context || !context->value_valid || !oil_temp_c) return false;
    *oil_temp_c = context->oil_temp_c;
    return true;
}
