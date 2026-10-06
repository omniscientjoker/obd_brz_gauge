#pragma once

#include "obd_protocol_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Community-derived Ford Fusion/Mondeo BCM candidate. It is not part of the
// default composition because DID/header layouts must be verified per car.
const obd_rule_pack_t *obd_ford_tpms_candidate_rules_get(void);

#ifdef __cplusplus
}
#endif
