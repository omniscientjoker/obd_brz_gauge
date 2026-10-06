#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Protocol identifiers are deliberately independent from vehicle names. A
// vehicle composition can enable more than one protocol at the same time.
typedef enum {
    OBD_PROTOCOL_STANDARD = 0,
    OBD_PROTOCOL_UDS22,
    OBD_PROTOCOL_CAN_MONITOR,
    OBD_PROTOCOL_MODE21,
    OBD_PROTOCOL_COUNT,
} obd_protocol_id_t;

typedef enum {
    OBD_RULE_MODE01 = 0,
    OBD_RULE_MODE21,
    OBD_RULE_MODE22,
    OBD_RULE_CAN,
} obd_rule_kind_t;

// Declarative request/decoder description. The same shape supports standard
// PIDs, UDS DIDs, and CAN fields; protocol-specific state remains in a module.
typedef struct {
    uint16_t rule_id;
    obd_protocol_id_t protocol;
    obd_rule_kind_t kind;
    uint8_t service;
    uint32_t address;
    const char *header;
    uint8_t channel;
    uint8_t resp_byte;
    uint8_t resp_bytes;
    // CAN rules use these fields as the source bit range. For byte-oriented
    // OBD/UDS rules they remain zero and resp_byte/resp_bytes are used.
    uint8_t bit_offset;
    uint8_t bit_length;
    bool big_endian;
    float scale;
    float offset;
    float min_value;
    float max_value;
    uint32_t period_ms;
    uint8_t source_priority;
    // Stable scheduler slot. 0..11 preserve the legacy OBD cadence; 0xFF
    // denotes a passive rule (for example a CAN monitor field).
    uint8_t schedule_slot;
    uint8_t fallback_rank;
} obd_data_rule_t;

typedef struct {
    const char *name;
    const obd_data_rule_t *rules;
    uint16_t rule_count;
    uint8_t priority;
} obd_rule_pack_t;

typedef enum {
    OBD_OVERRIDE_APPEND = 0,
    OBD_OVERRIDE_REPLACE,
    OBD_OVERRIDE_DISABLE,
} obd_override_mode_t;

typedef struct {
    obd_override_mode_t mode;
    const obd_data_rule_t *rule;
    uint8_t priority;
    // Optional stable semantic identity for replacing a rule whose address or
    // header changes. Zero keeps the exact composite-key matching behavior.
    uint16_t target_rule_id;
} obd_rule_patch_t;

typedef struct {
    obd_protocol_id_t id;
    const char *name;

    bool (*init)(void *ctx);
    bool (*build_request)(const obd_data_rule_t *rule, char *out, size_t out_len);
    bool (*parse_response)(void *ctx, const char *response);
    void (*reset)(void *ctx);
} obd_protocol_module_t;

typedef struct {
    uint16_t handler_id;
    const char *name;

    void (*reset)(void *ctx);
    bool (*before_request)(void *ctx, char *out, size_t out_len);
    bool (*consume_response)(void *ctx, const char *response);
    void (*after_request)(void *ctx);
} obd_special_handler_t;

typedef struct {
    const obd_protocol_module_t *protocol;
    const obd_data_rule_t *rule;
    uint8_t slot_id;
    uint8_t fallback_rank;
    uint8_t priority;
    int64_t next_due_us;
    bool pending;
} obd_request_slot_t;

#define OBD_REQUEST_PLAN_MAX_SLOTS 64
typedef struct {
    obd_request_slot_t slots[OBD_REQUEST_PLAN_MAX_SLOTS];
    uint8_t slot_count;
} obd_request_plan_t;

#ifdef __cplusplus
}
#endif
