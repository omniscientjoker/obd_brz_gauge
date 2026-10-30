#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GAUGE_LINK_SYNC0       0xA5u
#define GAUGE_LINK_SYNC1       0x5Au
#define GAUGE_LINK_VERSION     1u
#define GAUGE_LINK_BAUDRATE    115200u
#define GAUGE_LINK_MAX_PAYLOAD 64u

typedef enum {
    GAUGE_LINK_FRAME_CONFIG = 1,
    GAUGE_LINK_FRAME_DATA = 2,
} gauge_link_frame_type_t;

typedef enum {
    GAUGE_LINK_TARGET_LEFT = 1,
    GAUGE_LINK_TARGET_RIGHT = 2,
} gauge_link_target_t;

typedef enum {
    GAUGE_LINK_DASH_RPM = 0,
    GAUGE_LINK_DASH_SPEED,
    GAUGE_LINK_DASH_TEMP,
    GAUGE_LINK_DASH_TPMS,
    GAUGE_LINK_DASH_FUEL,
    GAUGE_LINK_DASH_INFO,
    GAUGE_LINK_DASH_NEEDLE,
    GAUGE_LINK_DASH_CHART,
    GAUGE_LINK_DASH_COUNT,
} gauge_link_dashboard_t;

/* Wire values are little-endian and packed. All values use the same units as
 * obd_data_snapshot_t, so the receiver never needs to run vehicle parsing. */
typedef struct __attribute__((packed)) {
    uint8_t dashboard_id;
} gauge_link_config_t;

enum {
    GAUGE_LINK_VALID_RPM = 1u << 0,
    GAUGE_LINK_VALID_SPEED = 1u << 1,
    GAUGE_LINK_VALID_TEMP = 1u << 2,
    GAUGE_LINK_VALID_VOLTAGE = 1u << 3,
    GAUGE_LINK_VALID_FUEL = 1u << 4,
    GAUGE_LINK_VALID_TPMS_FL = 1u << 5,
    GAUGE_LINK_VALID_TPMS_FR = 1u << 6,
    GAUGE_LINK_VALID_TPMS_RL = 1u << 7,
    GAUGE_LINK_VALID_TPMS_RR = 1u << 8,
    GAUGE_LINK_VALID_OIL_TEMP = 1u << 9,
    GAUGE_LINK_VALID_INTAKE_TEMP = 1u << 10,
};

typedef struct __attribute__((packed)) {
    uint16_t rpm;
    uint16_t speed_kmh;
    int16_t coolant_temp_c;
    int16_t oil_temp_c;
    int16_t intake_temp_c;
    int32_t battery_mv;
    int16_t fuel_pct;
    int16_t tpms_bar_x100[4];
    uint16_t valid_mask;
} gauge_link_data_t;

typedef struct {
    uint8_t type;
    uint8_t target;
    uint32_t sequence;
    uint16_t length;
    uint8_t payload[GAUGE_LINK_MAX_PAYLOAD];
} gauge_link_frame_t;

typedef struct {
    uint8_t buffer[2u + 1u + 1u + 1u + 1u + 2u + 4u + GAUGE_LINK_MAX_PAYLOAD + 2u];
    size_t used;
} gauge_link_parser_t;

typedef bool (*gauge_link_frame_cb_t)(void *ctx, const gauge_link_frame_t *frame);

uint16_t gauge_link_crc16(const uint8_t *data, size_t length);
size_t gauge_link_encode(uint8_t type, uint8_t target, uint32_t sequence,
                         const void *payload, uint16_t payload_length,
                         uint8_t *out, size_t out_capacity);
void gauge_link_parser_init(gauge_link_parser_t *parser);
size_t gauge_link_parser_feed(gauge_link_parser_t *parser, const uint8_t *data,
                               size_t length, gauge_link_frame_cb_t callback,
                               void *ctx);

#ifdef __cplusplus
}
#endif
