#include "frame.h"
#include <string.h>

int frame_header_encode(uint8_t buf[BHTTP_HEADER_SIZE], const struct bhttp_frame_header *hdr) {
    if (!buf || !hdr) {
        return -1;
    }
    if (hdr->length > BHTTP_MAX_PAYLOAD_SIZE) {
        return -1;
    }

    encode_u24(buf, hdr->length);
    buf[3] = hdr->type;
    buf[4] = hdr->flags;
    /* Mask top bit to ensure Reserved bit is 0 */
    encode_u32(buf + 5, hdr->stream_id & 0x7FFFFFFF);
    return 0;
}

int frame_header_decode(struct bhttp_frame_header *hdr, const uint8_t buf[BHTTP_HEADER_SIZE]) {
    if (!hdr || !buf) {
        return -1;
    }

    hdr->length = decode_u24(buf);
    hdr->type = buf[3];
    hdr->flags = buf[4];
    /* Top bit is masked off per protocol specification */
    hdr->stream_id = decode_u32(buf + 5) & 0x7FFFFFFF;
    return 0;
}
