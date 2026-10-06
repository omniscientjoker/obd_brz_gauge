#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "obd_composition.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OBD_RESPONSE_MODE01 = 0x41,
    OBD_RESPONSE_MODE21 = 0x61,
    OBD_RESPONSE_MODE22 = 0x62,
} obd_response_service_t;

typedef struct {
    // A decoded value is delivered once per matching rule. The sink owns any
    // filtering, cache writes, diagnostics, and source arbitration policy.
    void (*on_value)(void *ctx, const obd_data_rule_t *rule, float value);
    void (*on_frame_valid)(void *ctx, obd_response_service_t service);
} obd_response_sink_t;

typedef struct {
    const obd_composed_plan_t *plan;
    const obd_response_sink_t *sink;
    void *ctx;
} obd_response_dispatcher_t;

typedef struct {
    const obd_response_dispatcher_t *dispatcher;
    const char *header;
} obd_text_response_context_t;

// Decode one normalized Mode 01 response. payload starts immediately after
// the PID and contains only response data bytes.
bool obd_response_dispatch_mode01(const obd_response_dispatcher_t *dispatcher,
                                  uint8_t pid,
                                  const uint8_t *payload,
                                  size_t payload_len,
                                  const char *header);

// Decode one normalized UDS/Mode 22 response. payload starts after the DID.
bool obd_response_dispatch_mode22(const obd_response_dispatcher_t *dispatcher,
                                  uint16_t did,
                                  const uint8_t *payload,
                                  size_t payload_len,
                                  const char *header);

// Decode one CAN frame using OBD_RULE_CAN entries in the active plan.
bool obd_response_dispatch_can(const obd_response_dispatcher_t *dispatcher,
                               uint32_t can_id,
                               const uint8_t *payload,
                               size_t payload_len,
                               const char *header);

// Parse one complete ELM text response and dispatch the first matching
// normalized frame. These functions deliberately ignore prompts, adapter
// headers and ISO-TP line labels, leaving only service/PID/DID/payload to the
// declarative dispatcher.
bool obd_response_dispatch_text_mode01(const obd_text_response_context_t *context,
                                       const char *response);
bool obd_response_dispatch_text_mode22(const obd_text_response_context_t *context,
                                       const char *response);

// Parse one complete ELM ATMA/CAN monitor line. Accepted forms include
// "140 00 00 7F" and "0: 140 00 00 7F"; an optional DLC byte is accepted
// when it exactly describes the number of following data bytes.
bool obd_response_dispatch_text_can(const obd_text_response_context_t *context,
                                    const char *response);

#ifdef __cplusplus
}
#endif
