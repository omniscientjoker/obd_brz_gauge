#pragma once

#include <stdbool.h>

#include "obd_protocol_types.h"
#include "vehicle_custom_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (*obd_special_command_sender_t)(void *ctx, const char *command);

// BMW EGS gear reads may require an extended-address raw CAN request and a
// temporary receive filter. Keep that adapter-specific command script outside
// the BLE transport and return false when the caller should use the normal
// declarative Mode 22 request path.
bool obd_special_bmw_is_raw_gear(const obd_data_rule_t *rule,
                                 const vehicle_override_t *override);

bool obd_special_bmw_send_raw_gear(const vehicle_override_t *override,
                                   const char *fixed_header,
                                   obd_special_command_sender_t send,
                                   void *ctx);

// Capability descriptor for compositions that use BMW EGS extended-address
// requests. The transport-bound command script above remains authoritative.
#define OBD_SPECIAL_HANDLER_BMW_EGS 0xB001
const obd_special_handler_t *obd_special_bmw_handler_get(void);

#ifdef __cplusplus
}
#endif
