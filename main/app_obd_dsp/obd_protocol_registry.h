#pragma once

#include "obd_protocol_types.h"

#ifdef __cplusplus
extern "C" {
#endif

const obd_protocol_module_t *obd_protocol_get(obd_protocol_id_t id);

bool obd_protocol_build_request(const obd_protocol_module_t *module,
                                const obd_data_rule_t *rule,
                                char *out,
                                size_t out_len);

#ifdef __cplusplus
}
#endif
