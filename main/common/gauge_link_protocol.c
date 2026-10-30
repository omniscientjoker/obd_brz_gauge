#include "gauge_link_protocol.h"

#include <string.h>

#define FIXED_SIZE 11u /* sync + version/type/target + length + sequence */

static uint16_t read_u16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void write_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void write_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

uint16_t gauge_link_crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc & 1u) ? (uint16_t)((crc >> 1) ^ 0xA001u) : (uint16_t)(crc >> 1);
    }
    return crc;
}

size_t gauge_link_encode(uint8_t type, uint8_t target, uint32_t sequence,
                         const void *payload, uint16_t payload_length,
                         uint8_t *out, size_t out_capacity)
{
    if (!out || payload_length > GAUGE_LINK_MAX_PAYLOAD || (payload_length && !payload)) return 0;
    size_t total = FIXED_SIZE + payload_length + 2u;
    if (out_capacity < total) return 0;
    out[0] = GAUGE_LINK_SYNC0;
    out[1] = GAUGE_LINK_SYNC1;
    out[2] = GAUGE_LINK_VERSION;
    out[3] = type;
    out[4] = target;
    write_u16(&out[5], payload_length);
    write_u32(&out[7], sequence);
    if (payload_length) memcpy(&out[11], payload, payload_length);
    write_u16(&out[11 + payload_length], gauge_link_crc16(&out[2], 9u + payload_length));
    return total;
}

void gauge_link_parser_init(gauge_link_parser_t *parser)
{
    if (parser) memset(parser, 0, sizeof(*parser));
}

static void discard_prefix(gauge_link_parser_t *parser, size_t count)
{
    if (count >= parser->used) {
        parser->used = 0;
        return;
    }
    memmove(parser->buffer, &parser->buffer[count], parser->used - count);
    parser->used -= count;
}

size_t gauge_link_parser_feed(gauge_link_parser_t *parser, const uint8_t *data,
                              size_t length, gauge_link_frame_cb_t callback, void *ctx)
{
    if (!parser || (!data && length)) return 0;
    size_t accepted = 0;
    const size_t capacity = sizeof(parser->buffer);
    for (size_t i = 0; i < length; ++i) {
        if (parser->used == capacity) discard_prefix(parser, 1);
        parser->buffer[parser->used++] = data[i];
        accepted++;
        for (;;) {
            if (parser->used < 2) break;
            size_t sync = 0;
            while (sync + 1 < parser->used &&
                   (parser->buffer[sync] != GAUGE_LINK_SYNC0 || parser->buffer[sync + 1] != GAUGE_LINK_SYNC1)) sync++;
            if (sync) discard_prefix(parser, sync);
            if (parser->used < FIXED_SIZE) break;
            if (parser->buffer[2] != GAUGE_LINK_VERSION) {
                discard_prefix(parser, 1);
                continue;
            }
            uint16_t payload_length = read_u16(&parser->buffer[5]);
            if (payload_length > GAUGE_LINK_MAX_PAYLOAD) {
                discard_prefix(parser, 2);
                continue;
            }
            size_t frame_size = FIXED_SIZE + payload_length + 2u;
            if (parser->used < frame_size) break;
            uint16_t expected = read_u16(&parser->buffer[11 + payload_length]);
            uint16_t actual = gauge_link_crc16(&parser->buffer[2], 9u + payload_length);
            if (actual != expected) {
                discard_prefix(parser, 2);
                continue;
            }
            gauge_link_frame_t frame = {
                .type = parser->buffer[3], .target = parser->buffer[4],
                .sequence = read_u32(&parser->buffer[7]), .length = payload_length,
            };
            if (payload_length) memcpy(frame.payload, &parser->buffer[11], payload_length);
            discard_prefix(parser, frame_size);
            if (callback) callback(ctx, &frame);
            break;
        }
    }
    return accepted;
}
