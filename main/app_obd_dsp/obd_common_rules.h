#pragma once

#include "obd_protocol_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Standard SAE J1979 rules executed by the composition request/response path.
const obd_rule_pack_t *obd_common_rules_get(void);

#ifdef __cplusplus
}
#endif
