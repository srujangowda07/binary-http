#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "frame.h"
#include "protocol.h"

static void test_frame_header_roundtrip(void) {
    struct bhttp_frame_header orig = {
        .length = 39,
        .type = FRAME_TYPE_REQUEST,
        .flags = FLAG_END_STREAM,
        .stream_id = 1
    };

    uint8_t buf[BHTTP_HEADER_SIZE];
    assert(frame_header_encode(buf, &orig) == 0);

    /* Verify wire bytes explicitly */
    assert(buf[0] == 0x00 && buf[1] == 0x00 && buf[2] == 0x27);
    assert(buf[3] == 0x01);
    assert(buf[4] == 0x01);
    assert(buf[5] == 0x00 && buf[6] == 0x00 && buf[7] == 0x00 && buf[8] == 0x01);

    struct bhttp_frame_header decoded;
    assert(frame_header_decode(&decoded, buf) == 0);
    assert(decoded.length == orig.length);
    assert(decoded.type == orig.type);
    assert(decoded.flags == orig.flags);
    assert(decoded.stream_id == orig.stream_id);
    printf("  [PASS] test_frame_header_roundtrip\n");
}

static void test_frame_header_boundaries(void) {
    struct bhttp_frame_header hdr = {
        .length = BHTTP_MAX_PAYLOAD_SIZE, /* 16777215 */
        .type = 0xFE,
        .flags = 0xFF,
        .stream_id = 0x7FFFFFFF           /* 31-bit max */
    };

    uint8_t buf[BHTTP_HEADER_SIZE];
    assert(frame_header_encode(buf, &hdr) == 0);

    struct bhttp_frame_header decoded;
    assert(frame_header_decode(&decoded, buf) == 0);
    assert(decoded.length == BHTTP_MAX_PAYLOAD_SIZE);
    assert(decoded.type == 0xFE);
    assert(decoded.flags == 0xFF);
    assert(decoded.stream_id == 0x7FFFFFFF);

    /* Test reserved bit masking */
    hdr.stream_id = 0xFFFFFFFF; /* Top bit set */
    assert(frame_header_encode(buf, &hdr) == 0);
    assert((buf[5] & 0x80) == 0); /* Reserved bit MUST be 0 */
    assert(frame_header_decode(&decoded, buf) == 0);
    assert(decoded.stream_id == 0x7FFFFFFF);

    /* Test length exceeding protocol maximum */
    hdr.length = BHTTP_MAX_PAYLOAD_SIZE + 1;
    assert(frame_header_encode(buf, &hdr) == -1);

    printf("  [PASS] test_frame_header_boundaries\n");
}

int main(void) {
    printf("Running frame unit tests...\n");
    test_frame_header_roundtrip();
    test_frame_header_boundaries();
    printf("All frame unit tests passed successfully.\n");
    return 0;
}
