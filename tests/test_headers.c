#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "headers.h"

static void test_static_table_mapping(void) {
    assert(strcmp(header_id_to_name(HEADER_ID_METHOD), ":method") == 0);
    assert(strcmp(header_id_to_name(HEADER_ID_PATH), ":path") == 0);
    assert(strcmp(header_id_to_name(HEADER_ID_STATUS), ":status") == 0);
    assert(strcmp(header_id_to_name(HEADER_ID_CONTENT_LENGTH), "content-length") == 0);
    assert(strcmp(header_id_to_name(HEADER_ID_CONTENT_TYPE), "content-type") == 0);
    assert(strcmp(header_id_to_name(HEADER_ID_HOST), "host") == 0);
    assert(strcmp(header_id_to_name(HEADER_ID_USER_AGENT), "user-agent") == 0);
    assert(strcmp(header_id_to_name(HEADER_ID_SERVER), "server") == 0);
    assert(strcmp(header_id_to_name(HEADER_ID_ACCEPT), "accept") == 0);
    assert(strcmp(header_id_to_name(HEADER_ID_CONNECTION), "connection") == 0);
    assert(header_id_to_name(0) == NULL);
    assert(header_id_to_name(11) == NULL);

    assert(header_name_to_id(":method") == HEADER_ID_METHOD);
    assert(header_name_to_id(":PATH") == HEADER_ID_PATH); /* Case-insensitive */
    assert(header_name_to_id("Custom-Header") == HEADER_ID_LITERAL);
    printf("  [PASS] test_static_table_mapping\n");
}

static void test_headers_encode_decode(void) {
    struct bhttp_header_list orig;
    header_list_init(&orig);

    assert(header_list_add(&orig, ":method", "GET") == 0);
    assert(header_list_add(&orig, ":path", "/index.html") == 0);
    assert(header_list_add(&orig, "host", "localhost:9000") == 0);
    assert(header_list_add(&orig, "x-custom", "custom-val") == 0);

    uint8_t buf[512];
    ssize_t encoded = headers_encode(buf, sizeof(buf), &orig);
    assert(encoded > 0);

    struct bhttp_header_list decoded;
    size_t consumed = 0;
    assert(headers_decode(&decoded, buf, (size_t)encoded, &consumed) == 0);
    assert(consumed == (size_t)encoded);
    assert(decoded.count == 4);

    assert(strcmp(header_list_get(&decoded, ":method"), "GET") == 0);
    assert(strcmp(header_list_get(&decoded, ":path"), "/index.html") == 0);
    assert(strcmp(header_list_get(&decoded, "host"), "localhost:9000") == 0);
    assert(strcmp(header_list_get(&decoded, "x-custom"), "custom-val") == 0);

    header_list_free(&orig);
    header_list_free(&decoded);
    printf("  [PASS] test_headers_encode_decode\n");
}

static void test_headers_malformed(void) {
    struct bhttp_header_list decoded;
    size_t consumed = 0;
    uint8_t truncated[4] = { 0x00, 0x02, 0x01, 0x00 }; /* claims 2 headers, but truncated */

    assert(headers_decode(&decoded, truncated, sizeof(truncated), &consumed) == -1);

    uint8_t empty[1] = { 0x00 };
    assert(headers_decode(&decoded, empty, sizeof(empty), &consumed) == -1);

    printf("  [PASS] test_headers_malformed\n");
}

int main(void) {
    printf("Running header unit tests...\n");
    test_static_table_mapping();
    test_headers_encode_decode();
    test_headers_malformed();
    printf("All header unit tests passed successfully.\n");
    return 0;
}
