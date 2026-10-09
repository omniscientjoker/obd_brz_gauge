#include "obd_vehicle_compositions.h"

#include <string.h>

#include "ford_mondeo_tpms.h"
#include "obd_special_bmw.h"
#include "obd_special_mode21.h"

static bool profile_has_mode21(const vehicle_profile_t *profile)
{
    if (!profile) return false;
    const oil_temp_query_mode_t modes[] = {
        profile->oil_temp_strategy.primary,
        profile->oil_temp_strategy.secondary,
        profile->oil_temp_strategy.tertiary,
        profile->oil_temp_strategy.quaternary,
    };
    for (uint8_t i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) {
        if (modes[i] == OIL_TEMP_MODE_TOYOTA_21_01) return true;
    }
    return false;
}

bool obd_vehicle_compositions_apply(const vehicle_profile_t *profile,
                                    obd_vehicle_composition_t *composition)
{
    if (!profile || !composition) return false;

    const uint8_t original_pack_count = composition->rule_pack_count;
    const uint8_t original_handler_count = composition->handler_count;

    // BCMii TPMS is verified for this exact Mondeo configuration. Keep it
    // profile-scoped: Ford regional variants can use different BCM layouts.
    if (profile->name &&
        strcmp(profile->name, VEHICLE_PROFILE_NAME_FORD_MONDEO_2014) == 0 &&
        !obd_composition_add_rule_pack(composition,
                                       obd_ford_mondeo_2014_tpms_rules_get())) {
        composition->rule_pack_count = original_pack_count;
        composition->handler_count = original_handler_count;
        return false;
    }

    bool has_mode21 = profile_has_mode21(profile);
    const vehicle_override_t *override = composition->legacy_override;
    if (override &&
        ((override->oil_primary && override->oil_primary->type == OIL_SPECIAL) ||
         (override->oil_secondary && override->oil_secondary->type == OIL_SPECIAL))) {
        has_mode21 = true;
    }
    if (has_mode21 && !obd_composition_add_handler(composition,
                                                    obd_mode21_handler_get())) {
        composition->rule_pack_count = original_pack_count;
        composition->handler_count = original_handler_count;
        return false;
    }
    if (override && override->obd_gear_raw_frame &&
        !obd_composition_add_handler(composition,
                                     obd_special_bmw_handler_get())) {
        composition->rule_pack_count = original_pack_count;
        composition->handler_count = original_handler_count;
        return false;
    }
    return true;
}
