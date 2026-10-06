#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "obd_protocol_types.h"
#include "vehicle_profiles.h"

#ifdef __cplusplus
extern "C" {
#endif

// Set to 0 at compile time to run the original vehicle_profile/override
// transport path only. The default keeps the composed architecture enabled;
// this switch is intentionally local to the firmware and requires no NVS
// migration or runtime state.
#ifndef OBD_COMPOSITION_ENABLE
#define OBD_COMPOSITION_ENABLE 1
#endif

#define OBD_COMPOSITION_MAX_PROTOCOLS 8
#define OBD_COMPOSITION_MAX_PACKS 12
#define OBD_COMPOSITION_MAX_HANDLERS 8
#define OBD_COMPOSITION_MAX_PATCHES 24
#define OBD_COMPOSITION_MAX_RULES 96

typedef struct {
    const char *name;
    const vehicle_profile_t *mechanical_profile;
    const vehicle_override_t *legacy_override;

    const obd_protocol_module_t *protocols[OBD_COMPOSITION_MAX_PROTOCOLS];
    uint8_t protocol_count;
    const obd_rule_pack_t *rule_packs[OBD_COMPOSITION_MAX_PACKS];
    uint8_t rule_pack_count;
    const obd_rule_patch_t *patches[OBD_COMPOSITION_MAX_PATCHES];
    uint8_t patch_count;
    const obd_special_handler_t *handlers[OBD_COMPOSITION_MAX_HANDLERS];
    uint8_t handler_count;
} obd_vehicle_composition_t;

typedef struct {
    const obd_data_rule_t *rule;
    const obd_rule_pack_t *pack;
    const char *source_name;
    uint8_t priority;
    bool disabled;
} obd_composed_rule_t;

typedef struct {
    const vehicle_profile_t *mechanical_profile;
    const vehicle_override_t *legacy_override;
    obd_composed_rule_t rules[OBD_COMPOSITION_MAX_RULES];
    uint8_t rule_count;
    const obd_special_handler_t *handlers[OBD_COMPOSITION_MAX_HANDLERS];
    uint8_t handler_count;
} obd_composed_plan_t;

// Return a read-only composition view for the active profile. The view is
// rebuilt when the selected profile changes and contains common, vehicle and
// special-handler descriptors used by the runtime request plan.
const obd_vehicle_composition_t *obd_composition_get_active(void);

// Append immutable packs/handlers while a vehicle composition is assembled.
// These helpers centralize capacity checks so vehicle registries cannot
// silently write past the fixed composition arrays.
bool obd_composition_add_rule_pack(obd_vehicle_composition_t *composition,
                                   const obd_rule_pack_t *pack);
bool obd_composition_add_handler(obd_vehicle_composition_t *composition,
                                 const obd_special_handler_t *handler);

// Merge packs from low to high priority. Replacements are matched by protocol,
// kind, service, address, header, and output channel, never by array index.
bool obd_composition_build_plan(const obd_vehicle_composition_t *composition,
                                obd_composed_plan_t *out);

// Validate the immutable merged plan before it is handed to the transport
// scheduler. This is intentionally deterministic and allocation-free so a
// malformed vehicle pack fails closed at profile initialization.
bool obd_composition_validate_plan(const obd_composed_plan_t *plan);

const obd_composed_rule_t *obd_composition_find_rule(const obd_composed_plan_t *plan,
                                                     obd_protocol_id_t protocol,
                                                     obd_rule_kind_t kind,
                                                     uint8_t service,
                                                     uint32_t address,
                                                     const char *header,
                                                     uint8_t channel);

const obd_special_handler_t *obd_composition_find_handler(
    const obd_composed_plan_t *plan, uint16_t handler_id);

// Invoke one registered special handler. Keeping lookup and invocation in the
// composition layer prevents transport code from reaching into handler tables.
bool obd_composition_consume_handler(const obd_composed_plan_t *plan,
                                     uint16_t handler_id,
                                     void *ctx,
                                     const char *response);

#ifdef __cplusplus
}
#endif
