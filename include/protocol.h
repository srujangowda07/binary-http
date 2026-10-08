#ifndef BHTTP_PROTOCOL_H
#define BHTTP_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include "headers.h"


#define BHTTP_HEADER_SIZE 9
#define BHTTP_MAX_PAYLOAD_SIZE 16777215  /* 2^24 - 1 bytes (~16 MB) */

/* Core Frame Types */
#define FRAME_TYPE_REQUEST  0x01
#define FRAME_TYPE_RESPONSE 0x02

/* Frame Flags */
#define FLAG_END_STREAM     0x01

/* Status Codes */
#define STATUS_OK           200
#define STATUS_BAD_REQUEST  400
#define STATUS_NOT_FOUND    404
#define STATUS_SERVER_ERROR 500

/* Big-endian integer serialization */
static inline void encode_u16(uint8_t *buf, uint16_t val) {
    buf[0] = (uint8_t)((val >> 8) & 0xFF);
    buf[1] = (uint8_t)(val & 0xFF);
}

static inline uint16_t decode_u16(const uint8_t *buf) {
    return (uint16_t)(((uint16_t)buf[0] << 8) | (uint16_t)buf[1]);
}

static inline void encode_u24(uint8_t *buf, uint32_t val) {
    buf[0] = (uint8_t)((val >> 16) & 0xFF);
    buf[1] = (uint8_t)((val >> 8) & 0xFF);
    buf[2] = (uint8_t)(val & 0xFF);
}

static inline uint32_t decode_u24(const uint8_t *buf) {
    return ((uint32_t)buf[0] << 16) | ((uint32_t)buf[1] << 8) | (uint32_t)buf[2];
}

static inline void encode_u32(uint8_t *buf, uint32_t val) {
    buf[0] = (uint8_t)((val >> 24) & 0xFF);
    buf[1] = (uint8_t)((val >> 16) & 0xFF);
    buf[2] = (uint8_t)((val >> 8) & 0xFF);
    buf[3] = (uint8_t)(val & 0xFF);
}

static inline uint32_t decode_u32(const uint8_t *buf) {
    return ((uint32_t)buf[0] << 24) |
           ((uint32_t)buf[1] << 16) |
           ((uint32_t)buf[2] << 8)  |
           (uint32_t)buf[3];
}

struct bhttp_request {
    uint32_t stream_id;
    char *method;
    char *path;
    struct bhttp_header_list headers;
    uint8_t *body;
    size_t body_len;
};

struct bhttp_response {
    uint32_t stream_id;
    int status;
    struct bhttp_header_list headers;
    uint8_t *body;
    size_t body_len;
};

void bhttp_request_init(struct bhttp_request *req);
void bhttp_request_free(struct bhttp_request *req);

void bhttp_response_init(struct bhttp_response *res);
void bhttp_response_free(struct bhttp_response *res);

ssize_t bhttp_encode_request(uint8_t *buf, size_t max_len, const struct bhttp_request *req);
int bhttp_decode_request_payload(struct bhttp_request *req, uint32_t stream_id, const uint8_t *buf, size_t len);

ssize_t bhttp_encode_response(uint8_t *buf, size_t max_len, const struct bhttp_response *res);
int bhttp_decode_response_payload(struct bhttp_response *res, uint32_t stream_id, const uint8_t *buf, size_t len);

#endif /* BHTTP_PROTOCOL_H */
