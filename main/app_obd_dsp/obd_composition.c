#include "obd_composition.h"

#include <math.h>
#include <string.h>

#include "obd_common_rules.h"
#include "ford_mondeo_tpms.h"
#include "obd_legacy_adapter.h"
#include "obd_protocol_registry.h"
#include "obd_vehicle_compositions.h"

static obd_vehicle_composition_t s_active_composition;
static const vehicle_profile_t *s_active_profile;
static bool s_active_profile_valid;

bool obd_composition_add_handler(obd_vehicle_composition_t *composition,
                                  const obd_special_handler_t *handler)
{
    if (!composition || !handler || handler->handler_id == 0) return false;
    for (uint8_t i = 0; i < composition->handler_count; ++i) {
        if (composition->handlers[i] == handler ||
            (composition->handlers[i] &&
             composition->handlers[i]->handler_id == handler->handler_id)) {
            return true;
        }
    }
    if (composition->handler_count >= OBD_COMPOSITION_MAX_HANDLERS) return false;
    composition->handlers[composition->handler_count++] = handler;
    return true;
}

bool obd_composition_add_rule_pack(obd_vehicle_composition_t *composition,
                                   const obd_rule_pack_t *pack)
{
    if (!composition || !pack || !pack->rules || pack->rule_count == 0)
        return false;
    for (uint8_t i = 0; i < composition->rule_pack_count; ++i) {
        if (composition->rule_packs[i] == pack) return true;
    }
    if (composition->rule_pack_count >= OBD_COMPOSITION_MAX_PACKS) return false;
    composition->rule_packs[composition->rule_pack_count++] = pack;
    return true;
}

static bool strings_equal_or_null(const char *a, const char *b)
{
    if (a == b) return true;
    if (!a || !b) return false;
    return strcmp(a, b) == 0;
}

static bool rule_key_matches(const obd_data_rule_t *rule,
                             obd_protocol_id_t protocol,
                             obd_rule_kind_t kind,
                             uint8_t service,
                             uint32_t address,
                             const char *header,
                             uint8_t channel)
{
    return rule && rule->protocol == protocol && rule->kind == kind &&
           rule->service == service && rule->address == address &&
           rule->channel == channel && strings_equal_or_null(rule->header, header);
}

static int find_rule_index(const obd_composed_plan_t *plan, const obd_data_rule_t *rule)
{
    if (!plan || !rule) return -1;
    for (uint8_t i = 0; i < plan->rule_count; ++i) {
        if (rule_key_matches(plan->rules[i].rule, rule->protocol, rule->kind,
                             rule->service, rule->address, rule->header, rule->channel)) {
            return (int)i;
        }
    }
    return -1;
}

static int find_rule_id_index(const obd_composed_plan_t *plan, uint16_t rule_id)
{
    if (!plan || rule_id == 0) return -1;
    for (uint8_t i = 0; i < plan->rule_count; ++i) {
        if (plan->rules[i].rule && plan->rules[i].rule->rule_id == rule_id) {
            return (int)i;
        }
    }
    return -1;
}

const obd_vehicle_composition_t *obd_composition_get_active(void)
{
    const vehicle_profile_t *profile = vehicle_profile_get_active();
    if (s_active_profile_valid && profile == s_active_profile)
        return &s_active_composition;

    memset(&s_active_composition, 0, sizeof(s_active_composition));
    s_active_profile = profile;
    s_active_profile_valid = true;
    s_active_composition.name = profile ? profile->name : "unknown";
    s_active_composition.mechanical_profile = profile;
    s_active_composition.legacy_override = vehicle_profile_get_override();

    // Keep the stable standard protocol descriptor in every composition; the
    // legacy adapter and optional packs below contribute their own modules.
    s_active_composition.protocols[s_active_composition.protocol_count++] =
        obd_protocol_get(OBD_PROTOCOL_STANDARD);
    s_active_composition.rule_packs[s_active_composition.rule_pack_count++] =
        obd_common_rules_get();

    // Vehicle-specific packs remain profile-scoped. In particular, the
    // validated Mondeo BCMii TPMS pack must not affect other Ford variants.
    if (!obd_vehicle_compositions_apply(profile, &s_active_composition)) {
        // Keep the common/legacy composition usable if an optional registry
        // entry exceeds a fixed capacity or is malformed.
        s_active_composition.handler_count = 0;
    }

    const obd_rule_pack_t *legacy_pack = obd_legacy_rules_get(profile);
    if (legacy_pack && s_active_composition.rule_pack_count < OBD_COMPOSITION_MAX_PACKS) {
        s_active_composition.rule_packs[s_active_composition.rule_pack_count++] = legacy_pack;
        for (uint16_t i = 0; i < legacy_pack->rule_count; ++i) {
            const obd_data_rule_t *rule = &legacy_pack->rules[i];
            bool present = false;
            for (uint8_t j = 0; j < s_active_composition.protocol_count; ++j) {
                if (s_active_composition.protocols[j] &&
                    s_active_composition.protocols[j]->id == rule->protocol) {
                    present = true;
                    break;
                }
            }
            if (!present && s_active_composition.protocol_count < OBD_COMPOSITION_MAX_PROTOCOLS)
                s_active_composition.protocols[s_active_composition.protocol_count++] =
                    obd_protocol_get(rule->protocol);
        }
    }

    // Include protocol descriptors contributed by every optional pack, not
    // only the legacy adapter. The request planner resolves modules directly,
    // but this metadata is used for diagnostics and future protocol reset.
    for (uint8_t p = 0; p < s_active_composition.rule_pack_count; ++p) {
        const obd_rule_pack_t *pack = s_active_composition.rule_packs[p];
        if (!pack || !pack->rules) continue;
        for (uint16_t r = 0; r < pack->rule_count; ++r) {
            const obd_protocol_module_t *module = obd_protocol_get(pack->rules[r].protocol);
            bool present = false;
            for (uint8_t j = 0; j < s_active_composition.protocol_count; ++j) {
                if (s_active_composition.protocols[j] == module) {
                    present = true;
                    break;
                }
            }
            if (!present && module &&
                s_active_composition.protocol_count < OBD_COMPOSITION_MAX_PROTOCOLS) {
                s_active_composition.protocols[s_active_composition.protocol_count++] = module;
            }
        }
    }
    return &s_active_composition;
}

bool obd_composition_build_plan(const obd_vehicle_composition_t *composition,
                                obd_composed_plan_t *out)
{
    if (!composition || !out) return false;
    memset(out, 0, sizeof(*out));
    out->mechanical_profile = composition->mechanical_profile;
    out->legacy_override = composition->legacy_override;

    for (uint8_t i = 0; i < composition->handler_count; ++i) {
        const obd_special_handler_t *handler = composition->handlers[i];
        if (!handler || handler->handler_id == 0) continue;
        bool duplicate = false;
        for (uint8_t j = 0; j < out->handler_count; ++j) {
            if (out->handlers[j] &&
                out->handlers[j]->handler_id == handler->handler_id) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate && out->handler_count < OBD_COMPOSITION_MAX_HANDLERS)
            out->handlers[out->handler_count++] = handler;
    }

    for (uint8_t p = 0; p < composition->rule_pack_count; ++p) {
        const obd_rule_pack_t *pack = composition->rule_packs[p];
        if (!pack || !pack->rules) continue;
        for (uint16_t r = 0; r < pack->rule_count; ++r) {
            const obd_data_rule_t *rule = &pack->rules[r];
            // A non-zero stable rule_id is the semantic identity used by
            // rule packs. It must win over the request-key match so a
            // regional/ECU override can change DID or header in place.
            int index = rule->rule_id ? find_rule_id_index(out, rule->rule_id) : -1;
            if (index < 0) index = find_rule_index(out, rule);
            if (index >= 0) {
                const obd_rule_pack_t *old_pack = out->rules[index].pack;
                uint8_t old_priority = old_pack ? old_pack->priority : 0;
                if (pack->priority > old_priority ||
                    (pack->priority == old_priority &&
                     rule->fallback_rank < out->rules[index].rule->fallback_rank)) {
                    out->rules[index].rule = rule;
                    out->rules[index].pack = pack;
                    out->rules[index].source_name = pack->name;
                    out->rules[index].priority = pack->priority;
                    out->rules[index].disabled = false;
                }
                continue;
            }
            if (out->rule_count >= OBD_COMPOSITION_MAX_RULES) return false;
            out->rules[out->rule_count++] = (obd_composed_rule_t){
                .rule = rule,
                .pack = pack,
                .source_name = pack->name,
                .priority = pack->priority,
                .disabled = false,
            };
        }
    }

    // Patches are applied after packs. A patch can replace, append, or
    // disable a rule without mutating the immutable static rule tables.
    for (uint8_t p = 0; p < composition->patch_count; ++p) {
        const obd_rule_patch_t *patch = composition->patches[p];
        const obd_data_rule_t *rule = patch ? patch->rule : NULL;
        if (!patch || !rule) continue;
        int index = patch->target_rule_id ?
                    find_rule_id_index(out, patch->target_rule_id) :
                    find_rule_index(out, rule);
        if (index >= 0) {
            if (patch->priority < out->rules[index].priority) continue;
            out->rules[index].priority = patch->priority;
            out->rules[index].source_name = "override";
            if (patch->mode == OBD_OVERRIDE_DISABLE) {
                out->rules[index].disabled = true;
            } else {
                out->rules[index].rule = rule;
                out->rules[index].pack = NULL;
                out->rules[index].disabled = false;
            }
            continue;
        }
        if (patch->mode == OBD_OVERRIDE_DISABLE ||
            out->rule_count >= OBD_COMPOSITION_MAX_RULES) {
            if (patch->mode == OBD_OVERRIDE_DISABLE) continue;
            return false;
        }
        out->rules[out->rule_count++] = (obd_composed_rule_t){
            .rule = rule,
            .pack = NULL,
            .source_name = "override",
            .priority = patch->priority,
            .disabled = false,
        };
    }
    return true;
}

bool obd_composition_validate_plan(const obd_composed_plan_t *plan)
{
    if (!plan || plan->rule_count > OBD_COMPOSITION_MAX_RULES ||
        plan->handler_count > OBD_COMPOSITION_MAX_HANDLERS) {
        return false;
    }
    for (uint8_t i = 0; i < plan->handler_count; ++i) {
        if (!plan->handlers[i] || plan->handlers[i]->handler_id == 0) return false;
    }
    for (uint8_t i = 0; i < plan->rule_count; ++i) {
        const obd_composed_rule_t *entry = &plan->rules[i];
        const obd_data_rule_t *rule = entry->rule;
        if (!rule || rule->protocol >= OBD_PROTOCOL_COUNT ||
            rule->kind > OBD_RULE_CAN || rule->channel >= CH_COUNT ||
            !isfinite(rule->scale) || !isfinite(rule->offset) ||
            !isfinite(rule->min_value) || !isfinite(rule->max_value) ||
            rule->min_value > rule->max_value) {
            return false;
        }
        if (rule->schedule_slot != 0xFF &&
            rule->schedule_slot >= OBD_REQUEST_PLAN_MAX_SLOTS) {
            return false;
        }
        if (rule->kind == OBD_RULE_CAN) {
            if (rule->protocol != OBD_PROTOCOL_CAN_MONITOR || rule->service != 0 ||
                rule->bit_length == 0 || rule->bit_length > 32 ||
                (uint16_t)rule->bit_offset + rule->bit_length > 64) {
                return false;
            }
        } else {
            if (rule->kind == OBD_RULE_MODE01 &&
                (rule->protocol != OBD_PROTOCOL_STANDARD || rule->service != 0x01 ||
                 rule->address > 0xFFu)) {
                return false;
            }
            if (rule->kind == OBD_RULE_MODE21 &&
                (rule->protocol != OBD_PROTOCOL_MODE21 || rule->service != 0x21 ||
                 rule->address > 0xFFu)) {
                return false;
            }
            if (rule->kind == OBD_RULE_MODE22 &&
                (rule->protocol != OBD_PROTOCOL_UDS22 || rule->service != 0x22 ||
                 rule->address > 0xFFFFu)) {
                return false;
            }
            if (rule->resp_bytes == 0 || rule->resp_bytes > 4 ||
                (uint16_t)rule->resp_byte + rule->resp_bytes > 64) {
                return false;
            }
        }
        for (uint8_t j = 0; j < i; ++j) {
            const obd_data_rule_t *other = plan->rules[j].rule;
            if (rule->rule_id != 0 && other &&
                rule->rule_id == other->rule_id &&
                !rule_key_matches(rule, other->protocol, other->kind,
                                  other->service, other->address, other->header,
                                  other->channel)) {
                return false;
            }
        }
    }
    return true;
}

const obd_composed_rule_t *obd_composition_find_rule(const obd_composed_plan_t *plan,
                                                     obd_protocol_id_t protocol,
                                                     obd_rule_kind_t kind,
                                                     uint8_t service,
                                                     uint32_t address,
                                                     const char *header,
                                                     uint8_t channel)
{
    if (!plan) return NULL;
    for (uint8_t i = 0; i < plan->rule_count; ++i) {
        if (!plan->rules[i].disabled &&
            rule_key_matches(plan->rules[i].rule, protocol, kind, service,
                             address, header, channel)) {
            return &plan->rules[i];
        }
    }
    return NULL;
}

const obd_special_handler_t *obd_composition_find_handler(
    const obd_composed_plan_t *plan, uint16_t handler_id)
{
    if (!plan || handler_id == 0) return NULL;
    for (uint8_t i = 0; i < plan->handler_count; ++i) {
        if (plan->handlers[i] && plan->handlers[i]->handler_id == handler_id)
            return plan->handlers[i];
    }
    return NULL;
}

bool obd_composition_consume_handler(const obd_composed_plan_t *plan,
                                     uint16_t handler_id,
                                     void *ctx,
                                     const char *response)
{
    const obd_special_handler_t *handler =
        obd_composition_find_handler(plan, handler_id);
    return handler && handler->consume_response &&
           handler->consume_response(ctx, response);
}
