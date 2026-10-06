#pragma once

#include "obd_protocol_types.h"
#include "vehicle_profiles.h"

#ifdef __cplusplus
extern "C" {
#endif

// Convert the existing vehicle profile/override tables into an immutable rule
// pack. The runtime executes this pack through the same request/response path
// as newly authored vehicle packs.
const obd_rule_pack_t *obd_legacy_rules_get(const vehicle_profile_t *profile);

#ifdef __cplusplus
}
#endif
