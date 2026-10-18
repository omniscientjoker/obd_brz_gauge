#include "obd_manager.h"
#include <string.h>
#include "obd_data_cache.h"
#include "obd_tpms_cache.h"

void obd_manager_reset(obd_manager_t *manager)
{
    if (!manager) return;
    memset(manager, 0, sizeof(*manager));
    obd_channel_arbiter_reset(&manager->arbiter);
}

void obd_manager_reset_data(obd_manager_t *manager)
{
    obd_manager_reset(manager);
    obd_data_reset();
    obd_tpms_cache_reset();
}

bool obd_manager_rebuild(obd_manager_t *manager,
                         const obd_vehicle_composition_t *composition,
                         const obd_response_sink_t *sink,
                         void *ctx)
{
    if (!manager || !composition || !sink) return false;
    obd_manager_reset(manager);
    if (!obd_composition_build_plan(composition, &manager->composed_plan) ||
        !obd_composition_validate_plan(&manager->composed_plan) ||
        !obd_request_plan_build(composition, &manager->request_plan)) {
        obd_manager_reset(manager);
        return false;
    }
    manager->dispatcher = (obd_response_dispatcher_t){
        .plan = &manager->composed_plan,
        .sink = sink,
        .ctx = ctx,
    };
    manager->ready = true;
    return true;
}
