#include "obd_request_plan.h"

#include <string.h>

static bool should_replace(const obd_request_slot_t *old_slot,
                           const obd_composed_rule_t *candidate)
{
    if (!old_slot || !candidate || !candidate->rule) return false;
    if (candidate->priority != old_slot->priority)
        return candidate->priority > old_slot->priority;
    return candidate->rule->fallback_rank < old_slot->fallback_rank;
}

bool obd_request_plan_build(const obd_vehicle_composition_t *composition,
                            obd_request_plan_t *out)
{
    obd_composed_plan_t composed;
    if (!composition || !out || !obd_composition_build_plan(composition, &composed) ||
        !obd_composition_validate_plan(&composed))
        return false;
    memset(out, 0, sizeof(*out));

    for (uint8_t i = 0; i < composed.rule_count; ++i) {
        const obd_composed_rule_t *candidate = &composed.rules[i];
        const obd_data_rule_t *rule = candidate->rule;
        if (candidate->disabled || !rule || rule->schedule_slot == 0xFF ||
            rule->protocol == OBD_PROTOCOL_CAN_MONITOR) {
            continue;
        }
        const obd_protocol_module_t *module = obd_protocol_get(rule->protocol);
        if (!module || !module->build_request) continue;

        int found = -1;
        for (uint8_t j = 0; j < out->slot_count; ++j) {
            if (out->slots[j].slot_id == rule->schedule_slot &&
                out->slots[j].fallback_rank == rule->fallback_rank) {
                found = j;
                break;
            }
        }
        if (found >= 0) {
            if (should_replace(&out->slots[found], candidate)) {
                out->slots[found] = (obd_request_slot_t){
                    .protocol = module,
                    .rule = rule,
                    .slot_id = rule->schedule_slot,
                    .fallback_rank = rule->fallback_rank,
                    .priority = candidate->priority,
                    .next_due_us = 0,
                    .pending = false,
                };
            }
            continue;
        }
        if (out->slot_count >= OBD_REQUEST_PLAN_MAX_SLOTS) return false;
        out->slots[out->slot_count++] = (obd_request_slot_t){
            .protocol = module,
            .rule = rule,
            .slot_id = rule->schedule_slot,
            .fallback_rank = rule->fallback_rank,
            .priority = candidate->priority,
            .next_due_us = 0,
            .pending = false,
        };
    }

    // Keep the legacy twelve-slot cadence deterministic. Passive/special
    // slots are still represented by their assigned slot number.
    for (uint8_t i = 1; i < out->slot_count; ++i) {
        obd_request_slot_t value = out->slots[i];
        uint8_t j = i;
        while (j > 0 &&
               (out->slots[j - 1].slot_id > value.slot_id ||
                (out->slots[j - 1].slot_id == value.slot_id &&
                 out->slots[j - 1].fallback_rank > value.fallback_rank))) {
            out->slots[j] = out->slots[j - 1];
            --j;
        }
        out->slots[j] = value;
    }
    return true;
}

const obd_request_slot_t *obd_request_plan_find(const obd_request_plan_t *plan,
                                                uint8_t slot_id)
{
    if (!plan) return NULL;
    for (uint8_t i = 0; i < plan->slot_count; ++i)
        if (plan->slots[i].slot_id == slot_id) return &plan->slots[i];
    return NULL;
}

const obd_request_slot_t *obd_request_plan_find_fallback(
    const obd_request_plan_t *plan, uint8_t slot_id, uint8_t fallback_rank)
{
    const obd_request_slot_t *best = NULL;
    if (!plan) return NULL;
    for (uint8_t i = 0; i < plan->slot_count; ++i) {
        const obd_request_slot_t *candidate = &plan->slots[i];
        if (candidate->slot_id != slot_id ||
            candidate->fallback_rank < fallback_rank) continue;
        if (!best || candidate->fallback_rank < best->fallback_rank ||
            (candidate->fallback_rank == best->fallback_rank &&
             candidate->priority > best->priority)) {
            best = candidate;
        }
    }
    return best;
}
