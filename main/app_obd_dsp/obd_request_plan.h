#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "obd_composition.h"
#include "obd_protocol_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

bool obd_request_plan_build(const obd_vehicle_composition_t *composition,
                            obd_request_plan_t *out);

const obd_request_slot_t *obd_request_plan_find(const obd_request_plan_t *plan,
                                                uint8_t slot_id);

// Select the first candidate for a slot whose fallback rank is at least the
// requested rank. This preserves the primary/secondary rule chain without
// duplicating protocol scheduling logic.
const obd_request_slot_t *obd_request_plan_find_fallback(
    const obd_request_plan_t *plan, uint8_t slot_id, uint8_t fallback_rank);

#ifdef __cplusplus
}
#endif
