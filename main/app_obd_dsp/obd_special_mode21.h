#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "obd_protocol_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int adaptive_oil_index;
    int16_t last_oil_c;
    int hold_count;
    int64_t last_accepted_us;
    bool zc_profile;
} obd_mode21_state_t;

typedef struct {
    obd_mode21_state_t state;
    int16_t coolant_temp;
    int64_t now_us;
    int32_t oil_temp_c;
    bool value_valid;
} obd_mode21_handler_context_t;

void obd_mode21_reset(obd_mode21_state_t *state, bool zc_profile);

// Parse a complete Mode 21 01 response. Returns true when a validated oil
// temperature was produced. The response buffer is transport text and may
// contain ELM line prefixes and ISO-TP consecutive-frame markers.
bool obd_mode21_parse_response(obd_mode21_state_t *state,
                               const char *response,
                               int16_t coolant_temp,
                               int64_t now_us,
                               int32_t *oil_temp_c);

// Capability descriptor used by the vehicle composition. The BLE transport
// still owns the stateful response path during the compatibility migration;
// this descriptor makes Mode 21 visible to diagnostics and future dispatch.
#define OBD_SPECIAL_HANDLER_MODE21 0x2101
const obd_special_handler_t *obd_mode21_handler_get(void);

void obd_mode21_handler_init(obd_mode21_handler_context_t *context,
                             bool zc_profile);
void obd_mode21_handler_begin(obd_mode21_handler_context_t *context,
                              int16_t coolant_temp,
                              int64_t now_us);
bool obd_mode21_handler_get_value(const obd_mode21_handler_context_t *context,
                                  int32_t *oil_temp_c);

#ifdef __cplusplus
}
#endif
