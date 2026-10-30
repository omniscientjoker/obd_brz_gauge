#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "common/gauge_link_protocol.h"

struct test_context {
    unsigned count;
    gauge_link_frame_t last;
};

static bool capture(void *ctx, const gauge_link_frame_t *frame)
{
    struct test_context *test = ctx;
    test->count++;
    test->last = *frame;
    return true;
}

int main(void)
{
    const uint8_t payload[] = {0x10, 0x20, 0x30, 0x40};
    uint8_t frame[GAUGE_LINK_MAX_PAYLOAD + 32];
    size_t frame_len = gauge_link_encode(GAUGE_LINK_FRAME_DATA,
                                         GAUGE_LINK_TARGET_LEFT, 42,
                                         payload, sizeof(payload), frame,
                                         sizeof(frame));
    assert(frame_len > 0);

    gauge_link_parser_t parser;
    struct test_context test = {0};
    gauge_link_parser_init(&parser);

    /* Noise plus a split frame must resynchronize and emit exactly once. */
    const uint8_t noise[] = {0x00, 0x7F, 0xA5};
    gauge_link_parser_feed(&parser, noise, sizeof(noise), capture, &test);
    gauge_link_parser_feed(&parser, frame, 3, capture, &test);
    gauge_link_parser_feed(&parser, frame + 3, frame_len - 3, capture, &test);
    assert(test.count == 1);
    assert(test.last.type == GAUGE_LINK_FRAME_DATA);
    assert(test.last.target == GAUGE_LINK_TARGET_LEFT);
    assert(test.last.sequence == 42);
    assert(test.last.length == sizeof(payload));
    assert(memcmp(test.last.payload, payload, sizeof(payload)) == 0);

    /* A CRC failure is discarded without poisoning the next frame. */
    uint8_t bad[sizeof(frame)];
    memcpy(bad, frame, frame_len);
    bad[frame_len - 1] ^= 0xFF;
    gauge_link_parser_feed(&parser, bad, frame_len, capture, &test);
    assert(test.count == 1);

    /* Back-to-back frames are both emitted. */
    uint8_t joined[sizeof(frame) * 2];
    memcpy(joined, frame, frame_len);
    memcpy(joined + frame_len, frame, frame_len);
    gauge_link_parser_feed(&parser, joined, frame_len * 2, capture, &test);
    assert(test.count == 3);
    return 0;
}
