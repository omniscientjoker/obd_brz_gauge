#pragma once

#include <stdbool.h>

#include "obd_composition.h"

#ifdef __cplusplus
extern "C" {
#endif

// Apply profile-specific rule packs and special-handler capabilities. The
// function only appends immutable descriptors; protocol discovery and legacy
// rule adaptation remain in the composition core.
bool obd_vehicle_compositions_apply(const vehicle_profile_t *profile,
                                    obd_vehicle_composition_t *composition);

#ifdef __cplusplus
}
#endif
