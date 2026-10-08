#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "protocol.h"
#include "frame.h"
#include "headers.h"

static void test_spec_request_wire_exact(void) {
    /* Exact request as documented in SPEC.md section 13.1 */
    const uint8_t expected_wire[] = {
        0x00, 0x00, 0x27, 0x01, 0x01, 0x00, 0x00, 0x00, 0x01, /* Frame Header (9B): Len=39, Type=1, Flags=1, Stream=1 */
        0x00, 0x03,                                           /* Num Headers = 3 */
        0x01, 0x00, 0x03, 'G', 'E', 'T',                      /* ID=1 (:method), Len=3, "GET" */
        0x02, 0x00, 0x0B, '/', 'i', 'n', 'd', 'e', 'x', '.', 'h', 't', 'm', 'l', /* ID=2 (:path), Len=11, "/index.html" */
        0x06, 0x00, 0x0E, 'l', 'o', 'c', 'a', 'l', 'h', 'o', 's', 't', ':', '9', '0', '0', '0' /* ID=6 (host), Len=14 */
    };
    const size_t expected_wire_len = sizeof(expected_wire);
    assert(expected_wire_len == 48);

    struct bhttp_request req;
    bhttp_request_init(&req);
    req.stream_id = 1;
    req.method = strdup("GET");
    req.path = strdup("/index.html");
    header_list_add(&req.headers, "host", "localhost:9000");

    uint8_t wire_buf[256];
    ssize_t encoded_len = bhttp_encode_request(wire_buf, sizeof(wire_buf), &req);
    assert(encoded_len == (ssize_t)expected_wire_len);
    assert(memcmp(wire_buf, expected_wire, expected_wire_len) == 0);

    /* Decode from wire */
    struct bhttp_frame_header hdr;
    assert(frame_header_decode(&hdr, wire_buf) == 0);
    assert(hdr.length == 39);
    assert(hdr.type == FRAME_TYPE_REQUEST);
    assert(hdr.flags == FLAG_END_STREAM);
    assert(hdr.stream_id == 1);

    struct bhttp_request dec_req;
    assert(bhttp_decode_request_payload(&dec_req, hdr.stream_id, wire_buf + BHTTP_HEADER_SIZE, hdr.length) == 0);
    assert(dec_req.stream_id == 1);
    assert(strcmp(dec_req.method, "GET") == 0);
    assert(strcmp(dec_req.path, "/index.html") == 0);
    assert(strcmp(header_list_get(&dec_req.headers, "host"), "localhost:9000") == 0);

    bhttp_request_free(&req);
    bhttp_request_free(&dec_req);
    printf("  [PASS] test_spec_request_wire_exact\n");
}

static void test_spec_response_wire_exact(void) {
    /* Exact response as documented in SPEC.md section 13.2 */
    const uint8_t expected_wire[] = {
        0x00, 0x00, 0x36, 0x02, 0x01, 0x00, 0x00, 0x00, 0x01, /* Frame Header (9B): Len=54, Type=2, Flags=1, Stream=1 */
        0x00, 0x04,                                           /* Num Headers = 4 */
        0x03, 0x00, 0x03, '2', '0', '0',                      /* ID=3 (:status), Len=3, "200" */
        0x05, 0x00, 0x09, 't', 'e', 'x', 't', '/', 'h', 't', 'm', 'l', /* ID=5 (content-type), Len=9 */
        0x04, 0x00, 0x02, '1', '5',                           /* ID=4 (content-length), Len=2 */
        0x08, 0x00, 0x0B, 'o', 'b', 's', 'e', 'r', 'v', 'e', '/', '1', '.', '0', /* ID=8 (server), Len=11 */
        '<', 'h', '1', '>', 'H', 'e', 'l', 'l', 'o', '<', '/', 'h', '1', '>', '\n' /* Body (15B) */
    };
    const size_t expected_wire_len = sizeof(expected_wire);
    assert(expected_wire_len == 63);

    struct bhttp_response res;
    bhttp_response_init(&res);
    res.stream_id = 1;
    res.status = 200;
    header_list_add(&res.headers, "content-type", "text/html");
    header_list_add(&res.headers, "content-length", "15");
    header_list_add(&res.headers, "server", "observe/1.0");

    const char *body_str = "<h1>Hello</h1>\n";
    res.body = (uint8_t *)strdup(body_str);
    res.body_len = strlen(body_str);

    uint8_t wire_buf[256];
    ssize_t encoded_len = bhttp_encode_response(wire_buf, sizeof(wire_buf), &res);
    assert(encoded_len == (ssize_t)expected_wire_len);
    assert(memcmp(wire_buf, expected_wire, expected_wire_len) == 0);

    /* Decode from wire */
    struct bhttp_frame_header hdr;
    assert(frame_header_decode(&hdr, wire_buf) == 0);
    assert(hdr.length == 54);
    assert(hdr.type == FRAME_TYPE_RESPONSE);
    assert(hdr.flags == FLAG_END_STREAM);
    assert(hdr.stream_id == 1);

    struct bhttp_response dec_res;
    assert(bhttp_decode_response_payload(&dec_res, hdr.stream_id, wire_buf + BHTTP_HEADER_SIZE, hdr.length) == 0);
    assert(dec_res.stream_id == 1);
    assert(dec_res.status == 200);
    assert(dec_res.body_len == 15);
    assert(memcmp(dec_res.body, body_str, 15) == 0);

    bhttp_response_free(&res);
    bhttp_response_free(&dec_res);
    printf("  [PASS] test_spec_response_wire_exact\n");
}

int main(void) {
    printf("Running protocol specification wire validation tests...\n");
    test_spec_request_wire_exact();
    test_spec_response_wire_exact();
    printf("All protocol specification wire validation tests passed successfully.\n");
    return 0;
}
