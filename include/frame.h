#ifndef BHTTP_FRAME_H
#define BHTTP_FRAME_H

#include <stdint.h>
#include <stddef.h>
#include "protocol.h"

struct bhttp_frame_header {
    uint32_t length;    /* 24-bit payload length (0..16777215) */
    uint8_t  type;      /* Frame type (0x01..0xFF) */
    uint8_t  flags;     /* Frame flags (e.g. FLAG_END_STREAM) */
    uint32_t stream_id; /* 31-bit stream ID */
};

int frame_header_encode(uint8_t buf[BHTTP_HEADER_SIZE], const struct bhttp_frame_header *hdr);
int frame_header_decode(struct bhttp_frame_header *hdr, const uint8_t buf[BHTTP_HEADER_SIZE]);

#endif /* BHTTP_FRAME_H */
