#pragma once

#include "obd_protocol_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Validated BCMii TPMS rules for the captured 2014 Mondeo 2.0T. This remains
// an explicit profile so other Ford variants do not inherit its ECU address.
const obd_rule_pack_t *obd_ford_mondeo_2014_tpms_rules_get(void);

#ifdef __cplusplus
}
#endif
