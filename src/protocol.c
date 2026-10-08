#include "protocol.h"
#include "frame.h"
#include "headers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void bhttp_request_init(struct bhttp_request *req) {
    if (!req) return;
    memset(req, 0, sizeof(*req));
    header_list_init(&req->headers);
}

void bhttp_request_free(struct bhttp_request *req) {
    if (!req) return;
    header_list_free(&req->headers);
    if (req->method) free(req->method);
    if (req->path) free(req->path);
    if (req->body) free(req->body);
    memset(req, 0, sizeof(*req));
}

void bhttp_response_init(struct bhttp_response *res) {
    if (!res) return;
    memset(res, 0, sizeof(*res));
    header_list_init(&res->headers);
}

void bhttp_response_free(struct bhttp_response *res) {
    if (!res) return;
    header_list_free(&res->headers);
    if (res->body) free(res->body);
    memset(res, 0, sizeof(*res));
}

ssize_t bhttp_encode_request(uint8_t *buf, size_t max_len, const struct bhttp_request *req) {
    if (!buf || !req || max_len < BHTTP_HEADER_SIZE) return -1;

    /* Ensure required pseudo-headers are in the list if provided on struct */
    struct bhttp_header_list hdr_copy;
    header_list_init(&hdr_copy);

    if (req->method && !header_list_get(&req->headers, ":method")) {
        header_list_add(&hdr_copy, ":method", req->method);
    }
    if (req->path && !header_list_get(&req->headers, ":path")) {
        header_list_add(&hdr_copy, ":path", req->path);
    }

    for (size_t i = 0; i < req->headers.count; i++) {
        const struct bhttp_header *h = &req->headers.items[i];
        header_list_add_binary(&hdr_copy, h->id, h->name, h->name_len, h->value, h->value_len);
    }

    ssize_t hdr_bytes = headers_encode(buf + BHTTP_HEADER_SIZE, max_len - BHTTP_HEADER_SIZE, &hdr_copy);
    header_list_free(&hdr_copy);
    if (hdr_bytes < 0) return -1;

    size_t payload_len = (size_t)hdr_bytes + req->body_len;
    if (payload_len > BHTTP_MAX_PAYLOAD_SIZE || BHTTP_HEADER_SIZE + payload_len > max_len) {
        return -1;
    }

    if (req->body_len > 0 && req->body) {
        memcpy(buf + BHTTP_HEADER_SIZE + hdr_bytes, req->body, req->body_len);
    }

    struct bhttp_frame_header fhdr;
    fhdr.length = (uint32_t)payload_len;
    fhdr.type = FRAME_TYPE_REQUEST;
    fhdr.flags = FLAG_END_STREAM;
    fhdr.stream_id = req->stream_id;

    if (frame_header_encode(buf, &fhdr) < 0) {
        return -1;
    }

    return (ssize_t)(BHTTP_HEADER_SIZE + payload_len);
}

int bhttp_decode_request_payload(struct bhttp_request *req, uint32_t stream_id, const uint8_t *buf, size_t len) {
    if (!req || !buf) return -1;

    bhttp_request_init(req);
    req->stream_id = stream_id;

    size_t consumed = 0;
    if (headers_decode(&req->headers, buf, len, &consumed) < 0) {
        bhttp_request_free(req);
        return -1;
    }

    const char *m = header_list_get(&req->headers, ":method");
    const char *p = header_list_get(&req->headers, ":path");
    if (!m || !p) {
        bhttp_request_free(req);
        return -1;
    }

    req->method = strdup(m);
    req->path = strdup(p);
    if (!req->method || !req->path) {
        bhttp_request_free(req);
        return -1;
    }

    size_t body_len = len - consumed;
    req->body_len = body_len;
    if (body_len > 0) {
        req->body = malloc(body_len);
        if (!req->body) {
            bhttp_request_free(req);
            return -1;
        }
        memcpy(req->body, buf + consumed, body_len);
    }

    return 0;
}

ssize_t bhttp_encode_response(uint8_t *buf, size_t max_len, const struct bhttp_response *res) {
    if (!buf || !res || max_len < BHTTP_HEADER_SIZE) return -1;

    struct bhttp_header_list hdr_copy;
    header_list_init(&hdr_copy);

    char status_str[16];
    snprintf(status_str, sizeof(status_str), "%d", res->status);
    if (!header_list_get(&res->headers, ":status")) {
        header_list_add(&hdr_copy, ":status", status_str);
    }

    for (size_t i = 0; i < res->headers.count; i++) {
        const struct bhttp_header *h = &res->headers.items[i];
        header_list_add_binary(&hdr_copy, h->id, h->name, h->name_len, h->value, h->value_len);
    }

    ssize_t hdr_bytes = headers_encode(buf + BHTTP_HEADER_SIZE, max_len - BHTTP_HEADER_SIZE, &hdr_copy);
    header_list_free(&hdr_copy);
    if (hdr_bytes < 0) return -1;

    size_t payload_len = (size_t)hdr_bytes + res->body_len;
    if (payload_len > BHTTP_MAX_PAYLOAD_SIZE || BHTTP_HEADER_SIZE + payload_len > max_len) {
        return -1;
    }

    if (res->body_len > 0 && res->body) {
        memcpy(buf + BHTTP_HEADER_SIZE + hdr_bytes, res->body, res->body_len);
    }

    struct bhttp_frame_header fhdr;
    fhdr.length = (uint32_t)payload_len;
    fhdr.type = FRAME_TYPE_RESPONSE;
    fhdr.flags = FLAG_END_STREAM;
    fhdr.stream_id = res->stream_id;

    if (frame_header_encode(buf, &fhdr) < 0) {
        return -1;
    }

    return (ssize_t)(BHTTP_HEADER_SIZE + payload_len);
}

int bhttp_decode_response_payload(struct bhttp_response *res, uint32_t stream_id, const uint8_t *buf, size_t len) {
    if (!res || !buf) return -1;

    bhttp_response_init(res);
    res->stream_id = stream_id;

    size_t consumed = 0;
    if (headers_decode(&res->headers, buf, len, &consumed) < 0) {
        bhttp_response_free(res);
        return -1;
    }

    const char *s = header_list_get(&res->headers, ":status");
    if (!s) {
        bhttp_response_free(res);
        return -1;
    }
    res->status = atoi(s);

    size_t body_len = len - consumed;
    res->body_len = body_len;
    if (body_len > 0) {
        res->body = malloc(body_len);
        if (!res->body) {
            bhttp_response_free(res);
            return -1;
        }
        memcpy(res->body, buf + consumed, body_len);
    }

    return 0;
}
