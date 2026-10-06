#include "obd_special_bmw.h"

bool obd_special_bmw_is_raw_gear(const obd_data_rule_t *rule,
                                 const vehicle_override_t *override)
{
    return rule && override && rule->channel == CH_GEAR_RAW &&
           override->obd_gear_raw_frame &&
           override->obd_gear_raw_frame[0] != '\0';
}

bool obd_special_bmw_send_raw_gear(const vehicle_override_t *override,
                                   const char *fixed_header,
                                   obd_special_command_sender_t send,
                                   void *ctx)
{
    if (!obd_special_bmw_is_raw_gear(
            &(obd_data_rule_t){.channel = CH_GEAR_RAW}, override) ||
        !fixed_header || !send) {
        return false;
    }

    bool ok = true;
    if (override->obd_gear_rx_filter_cmd &&
        !send(ctx, override->obd_gear_rx_filter_cmd)) ok = false;
    if (!send(ctx, "ATCAF0\r")) ok = false;
    if (!send(ctx, override->obd_gear_raw_frame)) ok = false;
    // Always restore adapter framing/filter state even when the raw request
    // failed, otherwise the next unrelated OBD slot inherits CAF0/CRA state.
    if (!send(ctx, "ATCAF1\r")) ok = false;
    if (override->obd_gear_rx_filter_cmd && !send(ctx, "ATCRA\r")) ok = false;
    if (!send(ctx, fixed_header)) ok = false;
    return ok;
}

static const obd_special_handler_t s_bmw_handler = {
    .handler_id = OBD_SPECIAL_HANDLER_BMW_EGS,
    .name = "BMW EGS extended-address gear request",
};

const obd_special_handler_t *obd_special_bmw_handler_get(void)
{
    return &s_bmw_handler;
}
