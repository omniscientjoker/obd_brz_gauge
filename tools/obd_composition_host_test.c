#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "app_obd_dsp/obd_composition.h"
#include "app_obd_dsp/obd_response_dispatch.h"
#include "app_obd_dsp/obd_request_plan.h"
#include "app_obd_dsp/obd_channel_arbiter.h"
#include "app_obd_dsp/ford_tpms_candidate.h"
#include "app_obd_dsp/obd_special_mode21.h"
#include "app_obd_dsp/obd_special_bmw.h"
#include "app_obd_dsp/obd_vehicle_compositions.h"

// The host test links only the merge engine. Runtime profile/registry symbols
// are stubbed because this test intentionally does not exercise ESP-IDF code.
static const obd_rule_pack_t *s_common_pack;

const vehicle_profile_t *vehicle_profile_get_active(void) { return NULL; }
const vehicle_override_t *vehicle_profile_get_override(void) { return NULL; }
const obd_rule_pack_t *obd_common_rules_get(void) { return s_common_pack; }
const obd_rule_pack_t *obd_legacy_rules_get(const vehicle_profile_t *profile)
{
    (void)profile;
    return NULL;
}
static obd_data_rule_t rule(uint32_t address, const char *header, uint8_t channel)
{
    obd_data_rule_t out = {0};
    out.rule_id = (uint16_t)address;
    out.protocol = OBD_PROTOCOL_UDS22;
    out.kind = OBD_RULE_MODE22;
    out.service = 0x22;
    out.address = address;
    out.header = header;
    out.channel = channel;
    out.resp_bytes = 1;
    out.scale = 1.0f;
    return out;
}

static void test_priority_and_disable(void)
{
    obd_data_rule_t base_rule = rule(0x2813, NULL, 0);
    obd_data_rule_t replacement = base_rule;
    replacement.scale = 0.05f;
    obd_rule_pack_t base = {"base", &base_rule, 1, 0};
    obd_rule_pack_t high = {"high", &replacement, 1, 10};
    obd_rule_patch_t disable = {
        .mode = OBD_OVERRIDE_DISABLE,
        .rule = &replacement,
        .priority = 20,
    };
    obd_data_rule_t moved = base_rule;
    moved.address = 0x2814;
    obd_rule_patch_t move_patch = {
        .mode = OBD_OVERRIDE_REPLACE,
        .rule = &moved,
        .priority = 20,
        .target_rule_id = base_rule.rule_id,
    };
    obd_vehicle_composition_t composition = {0};
    obd_composed_plan_t plan;

    composition.rule_packs[0] = &base;
    composition.rule_packs[1] = &high;
    composition.rule_pack_count = 2;
    assert(obd_composition_build_plan(&composition, &plan));
    assert(plan.rule_count == 1);
    assert(plan.rules[0].rule == &replacement);
    assert(plan.rules[0].priority == 10);
    assert(strcmp(plan.rules[0].source_name, "high") == 0);

    composition.patch_count = 1;
    composition.patches[0] = &disable;
    assert(obd_composition_build_plan(&composition, &plan));
    assert(plan.rule_count == 1);
    assert(plan.rules[0].disabled);
    assert(obd_composition_find_rule(&plan, OBD_PROTOCOL_UDS22,
                                     OBD_RULE_MODE22, 0x22, 0x2813,
                                     NULL, 0) == NULL);

    composition.patch_count = 1;
    composition.patches[0] = &move_patch;
    assert(obd_composition_build_plan(&composition, &plan));
    assert(plan.rule_count == 1);
    assert(plan.rules[0].rule == &moved);
    assert(obd_composition_find_rule(&plan, OBD_PROTOCOL_UDS22,
                                     OBD_RULE_MODE22, 0x22, 0x2814,
                                     NULL, 0)->rule == &moved);

    // Pack-level replacement uses the stable semantic ID even when the
    // replacement changes the request address and header.
    obd_data_rule_t pack_moved = base_rule;
    pack_moved.address = 0x2815;
    pack_moved.header = "ATSH726";
    obd_rule_pack_t pack_override = {"pack-override", &pack_moved, 1, 30};
    composition.patch_count = 0;
    composition.rule_packs[0] = &base;
    composition.rule_packs[1] = &pack_override;
    composition.rule_pack_count = 2;
    assert(obd_composition_build_plan(&composition, &plan));
    assert(plan.rule_count == 1);
    assert(plan.rules[0].rule == &pack_moved);
}

static void test_special_handler_composition(void)
{
    static const obd_special_handler_t handler = {
        .handler_id = 0x42,
        .name = "test-handler",
    };
    obd_vehicle_composition_t composition = {0};
    obd_composed_plan_t plan;
    composition.handlers[0] = &handler;
    composition.handlers[1] = &handler;
    composition.handler_count = 2;
    assert(obd_composition_build_plan(&composition, &plan));
    assert(plan.handler_count == 1);
    assert(obd_composition_find_handler(&plan, 0x42) == &handler);
    assert(obd_composition_find_handler(&plan, 0x99) == NULL);
    assert(!obd_composition_consume_handler(&plan, 0x42, NULL, "response"));
    assert(obd_mode21_handler_get()->handler_id == OBD_SPECIAL_HANDLER_MODE21);
    assert(obd_special_bmw_handler_get()->handler_id == OBD_SPECIAL_HANDLER_BMW_EGS);
}

static void test_vehicle_composition_registry(void)
{
    vehicle_profile_t ford = {0};
    ford.name = "Ford Mondeo 2014 TPMS candidate";
    ford.oil_temp_strategy.primary = OIL_TEMP_MODE_TOYOTA_21_01;
    obd_vehicle_composition_t composition = {0};
    composition.legacy_override = NULL;
    assert(obd_vehicle_compositions_apply(&ford, &composition));
    assert(composition.rule_pack_count == 1);
    assert(composition.handler_count == 1);
    assert(composition.handlers[0]->handler_id == OBD_SPECIAL_HANDLER_MODE21);

    vehicle_profile_t ordinary_mondeo = {0};
    ordinary_mondeo.name = "Ford Mondeo 2014 standard";
    ordinary_mondeo.oil_temp_strategy.primary = OIL_TEMP_MODE_PID_5C;
    obd_vehicle_composition_t ordinary_composition = {0};
    assert(obd_vehicle_compositions_apply(&ordinary_mondeo,
                                          &ordinary_composition));
    assert(ordinary_composition.rule_pack_count == 0);
    assert(ordinary_composition.handler_count == 0);

    const vehicle_override_t bmw_override = {
        .obd_gear_raw_frame = "18 DA 6F 1 03 22 DA 2E\r",
    };
    vehicle_profile_t bmw = {0};
    bmw.name = "BMW test";
    obd_vehicle_composition_t bmw_composition = {
        .legacy_override = &bmw_override,
    };
    assert(obd_vehicle_compositions_apply(&bmw, &bmw_composition));
    assert(bmw_composition.rule_pack_count == 0);
    assert(bmw_composition.handler_count == 1);
    assert(bmw_composition.handlers[0]->handler_id == OBD_SPECIAL_HANDLER_BMW_EGS);
}

static void test_header_is_part_of_key(void)
{
    obd_data_rule_t physical = rule(0x2813, "ATSH726", 0);
    obd_data_rule_t bcm = rule(0x2813, "ATSH7E0", 0);
    physical.rule_id = 0x7201;
    bcm.rule_id = 0x7E01;
    obd_rule_pack_t pack = {"headers", (obd_data_rule_t[]){physical, bcm}, 2, 1};
    obd_vehicle_composition_t composition = {0};
    obd_composed_plan_t plan;
    composition.rule_packs[0] = &pack;
    composition.rule_pack_count = 1;

    assert(obd_composition_build_plan(&composition, &plan));
    assert(plan.rule_count == 2);
    assert(obd_composition_find_rule(&plan, OBD_PROTOCOL_UDS22,
                                     OBD_RULE_MODE22, 0x22, 0x2813,
                                     "ATSH726", 0)->rule == &pack.rules[0]);
    assert(obd_composition_find_rule(&plan, OBD_PROTOCOL_UDS22,
                                     OBD_RULE_MODE22, 0x22, 0x2813,
                                     "ATSH7E0", 0)->rule == &pack.rules[1]);
}

static void test_capacity_failure(void)
{
    obd_data_rule_t rules[OBD_COMPOSITION_MAX_RULES + 1];
    obd_rule_pack_t pack;
    obd_vehicle_composition_t composition = {0};
    obd_composed_plan_t plan;
    for (uint16_t i = 0; i < sizeof(rules) / sizeof(rules[0]); ++i) {
        rules[i] = rule(0x1000u + i, NULL, 0);
    }
    pack = (obd_rule_pack_t){"overflow", rules,
                             (uint16_t)(sizeof(rules) / sizeof(rules[0])), 1};
    composition.rule_packs[0] = &pack;
    composition.rule_pack_count = 1;
    assert(!obd_composition_build_plan(&composition, &plan));
}

static void test_plan_validation(void)
{
    obd_data_rule_t valid = rule(0x010C, NULL, CH_RPM);
    valid.protocol = OBD_PROTOCOL_STANDARD;
    valid.kind = OBD_RULE_MODE01;
    valid.service = 0x01;
    valid.address = 0x0C;
    valid.resp_bytes = 2;
    valid.big_endian = true;
    valid.min_value = 0.0f;
    valid.max_value = 16383.0f;
    obd_rule_pack_t pack = {"valid", &valid, 1, 1};
    obd_vehicle_composition_t composition = {0};
    obd_composed_plan_t plan;
    composition.rule_packs[0] = &pack;
    composition.rule_pack_count = 1;
    assert(obd_composition_build_plan(&composition, &plan));
    assert(obd_composition_validate_plan(&plan));

    obd_data_rule_t invalid = valid;
    invalid.address = 0x100;
    obd_rule_pack_t invalid_pack = {"invalid", &invalid, 1, 2};
    composition.rule_packs[1] = &invalid_pack;
    composition.rule_pack_count = 2;
    assert(obd_composition_build_plan(&composition, &plan));
    assert(!obd_composition_validate_plan(&plan));
}

typedef struct {
    uint16_t rule_id;
    float value;
    unsigned count;
} sink_state_t;

static void capture_value(void *ctx, const obd_data_rule_t *rule, float value)
{
    sink_state_t *state = ctx;
    state->rule_id = rule->rule_id;
    state->value = value;
    state->count++;
}

static void test_response_dispatch(void)
{
    obd_data_rule_t mode01 = rule(0x010C, NULL, 0);
    mode01.address = 0x0C;
    mode01.protocol = OBD_PROTOCOL_STANDARD;
    mode01.kind = OBD_RULE_MODE01;
    mode01.service = 0x01;
    mode01.resp_bytes = 2;
    mode01.big_endian = true;
    mode01.scale = 0.25f;
    mode01.min_value = 0.0f;
    mode01.max_value = 16383.0f;

    obd_data_rule_t mode22 = rule(0x2813, "ATSH726", 1);
    mode22.resp_bytes = 2;
    mode22.big_endian = true;
    mode22.scale = 0.05f * 0.0689475729f;
    mode22.min_value = 0.0f;
    mode22.max_value = 100.0f;

    obd_data_rule_t can = rule(0x140, NULL, 2);
    can.protocol = OBD_PROTOCOL_CAN_MONITOR;
    can.kind = OBD_RULE_CAN;
    can.service = 0;
    can.bit_offset = 8;
    can.bit_length = 8;
    can.resp_bytes = 1;
    can.scale = 1.0f;
    can.min_value = 0.0f;
    can.max_value = 255.0f;

    obd_rule_pack_t pack = {"dispatch", (obd_data_rule_t[]){mode01, mode22, can}, 3, 1};
    obd_vehicle_composition_t composition = {0};
    obd_composed_plan_t plan;
    sink_state_t state = {0};
    const obd_response_sink_t sink = {.on_value = capture_value};
    obd_response_dispatcher_t dispatcher;
    composition.rule_packs[0] = &pack;
    composition.rule_pack_count = 1;
    assert(obd_composition_build_plan(&composition, &plan));
    dispatcher = (obd_response_dispatcher_t){.plan = &plan, .sink = &sink, .ctx = &state};

    assert(obd_response_dispatch_mode01(&dispatcher, 0x0C,
                                        (const uint8_t[]){0x1A, 0xF8}, 2, NULL));
    assert(state.rule_id == 0x010C && state.value == 1726.0f);
    assert(obd_response_dispatch_mode22(&dispatcher, 0x2813,
                                        (const uint8_t[]){0x02, 0x80}, 2, "ATSH726"));
    assert(state.rule_id == 0x2813 && state.value > 2.20f && state.value < 2.22f);
    assert(obd_response_dispatch_can(&dispatcher, 0x140,
                                     (const uint8_t[]){0x00, 0x7F}, 2, NULL));
    assert(state.rule_id == 0x0140 && state.value == 127.0f);
    assert(state.count == 3);

    obd_text_response_context_t text_context = {
        .dispatcher = &dispatcher,
        .header = NULL,
    };
    assert(obd_response_dispatch_text_mode01(&text_context,
                                             "7E8 41 0C 1A F8\r>"));
    text_context.header = "ATSH726";
    assert(obd_response_dispatch_text_mode22(&text_context,
                                             "62 28 13 02 80\r>"));
    assert(state.count == 5);
    const obd_protocol_module_t *standard_module =
        obd_protocol_get(OBD_PROTOCOL_STANDARD);
    const obd_protocol_module_t *uds_module = obd_protocol_get(OBD_PROTOCOL_UDS22);
    assert(standard_module && standard_module->parse_response);
    assert(uds_module && uds_module->parse_response);
    text_context.header = NULL;
    assert(standard_module->parse_response(&text_context,
                                           "41 0C 1A F8\r>"));
    text_context.header = "ATSH726";
    assert(uds_module->parse_response(&text_context,
                                     "62 28 13 02 80\r>"));
    assert(state.count == 7);

    const obd_protocol_module_t *can_module =
        obd_protocol_get(OBD_PROTOCOL_CAN_MONITOR);
    assert(can_module && can_module->parse_response);
    text_context.header = NULL;
    assert(can_module->parse_response(&text_context, "140 00 00 7F\r>"));
    assert(state.rule_id == 0x0140 && state.value == 0.0f);
    assert(can_module->parse_response(&text_context,
                                     " 0: 140 00 7F\r>"));
    assert(state.rule_id == 0x0140 && state.value == 127.0f);
    assert(can_module->parse_response(&text_context, "140 02 00 7F\r>"));
    assert(state.rule_id == 0x0140 && state.value == 127.0f);
    assert(state.count == 10);

    // A missing transport header must not guess when the same DID is present
    // for multiple ECUs. Supplying the in-flight header selects the rule.
    obd_data_rule_t same_did_a = mode22;
    obd_data_rule_t same_did_b = mode22;
    same_did_a.rule_id = 0x2814;
    same_did_a.header = "ATSH7E0";
    same_did_b.rule_id = 0x2815;
    same_did_b.header = "ATSH726";
    obd_rule_pack_t ambiguous_pack = {"ambiguous", (obd_data_rule_t[]){same_did_a, same_did_b}, 2, 1};
    obd_vehicle_composition_t ambiguous_composition = {0};
    obd_composed_plan_t ambiguous_plan;
    obd_response_dispatcher_t ambiguous_dispatcher;
    sink_state_t ambiguous_state = {0};
    ambiguous_composition.rule_packs[0] = &ambiguous_pack;
    ambiguous_composition.rule_pack_count = 1;
    assert(obd_composition_build_plan(&ambiguous_composition, &ambiguous_plan));
    ambiguous_dispatcher = (obd_response_dispatcher_t){
        .plan = &ambiguous_plan, .sink = &sink, .ctx = &ambiguous_state};
    assert(!obd_response_dispatch_mode22(&ambiguous_dispatcher, 0x2813,
                                         (const uint8_t[]){0x02, 0x80}, 2, NULL));
    assert(obd_response_dispatch_mode22(&ambiguous_dispatcher, 0x2813,
                                        (const uint8_t[]){0x02, 0x80}, 2, "ATSH726"));
    assert(ambiguous_state.rule_id == 0x2815);
}

static void test_request_plan(void)
{
    obd_data_rule_t rpm = rule(0x0C, NULL, 0);
    rpm.protocol = OBD_PROTOCOL_STANDARD;
    rpm.kind = OBD_RULE_MODE01;
    rpm.service = 0x01;
    rpm.schedule_slot = 0;
    rpm.resp_bytes = 2;
    rpm.big_endian = true;
    rpm.scale = 0.25f;
    rpm.min_value = 0.0f;
    rpm.max_value = 16383.0f;
    obd_data_rule_t oil = rule(0x2813, "ATSH726", 2);
    oil.schedule_slot = 6;
    oil.resp_bytes = 2;
    oil.big_endian = true;
    oil.min_value = 0.0f;
    oil.max_value = 100.0f;
    obd_rule_pack_t pack = {"plan", (obd_data_rule_t[]){rpm, oil}, 2, 1};
    obd_vehicle_composition_t composition = {0};
    obd_request_plan_t plan;
    char command[32];
    composition.rule_packs[0] = &pack;
    composition.rule_pack_count = 1;
    assert(obd_request_plan_build(&composition, &plan));
    assert(plan.slot_count == 2);
    assert(plan.slots[0].slot_id == 0 && plan.slots[1].slot_id == 6);
    assert(obd_protocol_build_request(plan.slots[0].protocol,
                                      plan.slots[0].rule, command,
                                      sizeof(command)));
    assert(strcmp(command, "01 0C\r") == 0);
    assert(obd_protocol_build_request(plan.slots[1].protocol,
                                      plan.slots[1].rule, command,
                                      sizeof(command)));
    assert(strcmp(command, "22 28 13\r") == 0);

    obd_data_rule_t oil_backup = oil;
    oil_backup.rule_id = 0x2814;
    oil_backup.address = 0x2814;
    oil_backup.fallback_rank = 1;
    obd_rule_pack_t fallback_pack = {"fallback", (obd_data_rule_t[]){oil, oil_backup}, 2, 1};
    obd_vehicle_composition_t fallback_composition = {0};
    obd_request_plan_t fallback_plan;
    fallback_composition.rule_packs[0] = &fallback_pack;
    fallback_composition.rule_pack_count = 1;
    assert(obd_request_plan_build(&fallback_composition, &fallback_plan));
    assert(fallback_plan.slot_count == 2);
    assert(obd_request_plan_find_fallback(&fallback_plan, 6, 1)->rule == &fallback_pack.rules[1]);
}

static void test_channel_arbiter(void)
{
    obd_channel_arbiter_t arbiter;
    obd_data_rule_t low = rule(0x010C, NULL, 0);
    obd_data_rule_t high = low;
    low.source_priority = 0;
    low.period_ms = 100;
    high.rule_id = 0x9001;
    high.source_priority = 20;
    obd_channel_arbiter_reset(&arbiter);
    assert(obd_channel_arbiter_publish(&arbiter, &low, 1.0f, 1000000));
    assert(obd_channel_arbiter_publish(&arbiter, &low, 2.0f, 1000100));
    assert(obd_channel_arbiter_publish(&arbiter, &high, 3.0f, 1000200));
    assert(obd_channel_arbiter_get(&arbiter, 0)->value == 3.0f);
    assert(!obd_channel_arbiter_publish(&arbiter, &low, 4.0f, 1000300));
    assert(obd_channel_arbiter_publish(&arbiter, &low, 5.0f, 2000000));
}

static void test_ford_tpms_candidate(void)
{
    const obd_rule_pack_t *pack = obd_ford_tpms_candidate_rules_get();
    obd_vehicle_composition_t composition = {0};
    obd_composed_plan_t plan;
    obd_request_plan_t request_plan;
    sink_state_t state = {0};
    const obd_response_sink_t sink = {.on_value = capture_value};
    obd_response_dispatcher_t dispatcher;
    char command[32];

    assert(pack && pack->rule_count == 4);
    composition.rule_packs[0] = pack;
    composition.rule_pack_count = 1;
    assert(obd_composition_build_plan(&composition, &plan));
    assert(obd_request_plan_build(&composition, &request_plan));
    assert(request_plan.slot_count == 4);
    static const char *const expected_commands[] = {
        "22 28 13\r", "22 28 14\r", "22 28 16\r", "22 28 15\r",
    };
    for (uint8_t slot = 12; slot <= 15; ++slot) {
        const obd_request_slot_t *request =
            obd_request_plan_find(&request_plan, slot);
        assert(request && request->rule);
        assert(obd_protocol_build_request(request->protocol, request->rule,
                                          command, sizeof(command)));
        assert(strcmp(command, expected_commands[slot - 12]) == 0);
    }
    dispatcher = (obd_response_dispatcher_t){.plan = &plan, .sink = &sink, .ctx = &state};
    assert(obd_response_dispatch_mode22(&dispatcher, 0x2813,
                                        (const uint8_t[]){0x02, 0x80}, 2,
                                        "ATSH726\r"));
    assert(state.rule_id == 0xF813);
    assert(state.value > 2.20f && state.value < 2.22f);
}

static void test_mode21_special_handler(void)
{
    obd_mode21_state_t state;
    char response[256] = "61 01";
    int32_t oil_c = 0;
    char byte[4];

    // 38 data bytes, with the ZN/C6 tail byte (index 33) equal to 100 => 60 C.
    for (int i = 0; i < 38; ++i) {
        snprintf(byte, sizeof(byte), " %02X", i == 33 ? 100 : 0);
        strncat(response, byte, sizeof(response) - strlen(response) - 1);
    }
    obd_mode21_reset(&state, true);
    assert(obd_mode21_parse_response(&state, response, -40, 1000000, &oil_c));
    assert(oil_c == 60);

    // A short/malformed response is rejected without changing the last value.
    assert(!obd_mode21_parse_response(&state, "61 01 00 01", -40,
                                       1100000, &oil_c));

    obd_mode21_handler_context_t handler_context;
    obd_mode21_handler_init(&handler_context, true);
    obd_mode21_handler_begin(&handler_context, -40, 1000000);
    const obd_special_handler_t *handler = obd_mode21_handler_get();
    assert(handler->consume_response(&handler_context, response));
    assert(obd_mode21_handler_get_value(&handler_context, &oil_c));
    assert(oil_c == 60);
}

typedef struct {
    const char *commands[8];
    unsigned count;
} command_script_state_t;

static bool capture_command(void *ctx, const char *command)
{
    command_script_state_t *state = ctx;
    if (!state || !command || state->count >= 8) return false;
    state->commands[state->count++] = command;
    return true;
}

static void test_bmw_special_script(void)
{
    const vehicle_override_t override = {
        .obd_gear_rx_filter_cmd = "ATCRA618\r",
        .obd_gear_raw_frame = "18 DA 6F 1 03 22 DA 2E\r",
    };
    obd_data_rule_t rule = { .channel = CH_GEAR_RAW };
    command_script_state_t state = {0};

    assert(obd_special_bmw_is_raw_gear(&rule, &override));
    assert(obd_special_bmw_send_raw_gear(&override, "ATSH7DF\r",
                                         capture_command, &state));
    assert(state.count == 6);
    assert(strcmp(state.commands[0], "ATCRA618\r") == 0);
    assert(strcmp(state.commands[1], "ATCAF0\r") == 0);
    assert(strcmp(state.commands[2], "18 DA 6F 1 03 22 DA 2E\r") == 0);
    assert(strcmp(state.commands[3], "ATCAF1\r") == 0);
    assert(strcmp(state.commands[4], "ATCRA\r") == 0);
    assert(strcmp(state.commands[5], "ATSH7DF\r") == 0);
}

int main(void)
{
    test_priority_and_disable();
    test_special_handler_composition();
    test_vehicle_composition_registry();
    test_header_is_part_of_key();
    test_capacity_failure();
    test_plan_validation();
    test_response_dispatch();
    test_request_plan();
    test_channel_arbiter();
    test_ford_tpms_candidate();
    test_mode21_special_handler();
    test_bmw_special_script();
    puts("obd_composition_host_test: PASS");
    return 0;
}
