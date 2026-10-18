#pragma once

#include <stdbool.h>

#include "obd_composition.h"
#include "obd_request_plan.h"
#include "obd_response_dispatch.h"
#include "obd_channel_arbiter.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Runtime owner for the selected vehicle's rules, request cadence and
 * response dispatcher. BLE is only a transport and feeds this state owner. */
typedef struct {
    obd_composed_plan_t composed_plan;
    obd_request_plan_t request_plan;
    obd_response_dispatcher_t dispatcher;
    obd_channel_arbiter_t arbiter;
    bool ready;
} obd_manager_t;

void obd_manager_reset(obd_manager_t *manager);
void obd_manager_reset_data(obd_manager_t *manager);
bool obd_manager_rebuild(obd_manager_t *manager,
                         const obd_vehicle_composition_t *composition,
                         const obd_response_sink_t *sink,
                         void *ctx);

#ifdef __cplusplus
}
#endif
