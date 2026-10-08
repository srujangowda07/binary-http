#ifndef BHTTP_HEADERS_H
#define BHTTP_HEADERS_H

#include <stdint.h>
#include <stddef.h>

#if defined(_MSC_VER)
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#elif defined(__has_include)
#if __has_include(<sys/types.h>)
#include <sys/types.h>
#elif defined(_WIN32)
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#else
typedef ptrdiff_t ssize_t;
#endif
#else
#include <sys/types.h>
#endif


#define BHTTP_MAX_HEADER_COUNT 128
#define BHTTP_MAX_HEADER_NAME_LEN 256
#define BHTTP_MAX_HEADER_VAL_LEN 65535

#define HEADER_ID_LITERAL        0x00
#define HEADER_ID_METHOD         0x01
#define HEADER_ID_PATH           0x02
#define HEADER_ID_STATUS         0x03
#define HEADER_ID_CONTENT_LENGTH 0x04
#define HEADER_ID_CONTENT_TYPE   0x05
#define HEADER_ID_HOST           0x06
#define HEADER_ID_USER_AGENT     0x07
#define HEADER_ID_SERVER         0x08
#define HEADER_ID_ACCEPT         0x09
#define HEADER_ID_CONNECTION     0x0A

#define STATIC_TABLE_SIZE 10

struct bhttp_header {
    uint8_t id;
    char *name;
    uint16_t name_len;
    char *value;
    uint16_t value_len;
};

struct bhttp_header_list {
    struct bhttp_header items[BHTTP_MAX_HEADER_COUNT];
    size_t count;
};

const char *header_id_to_name(uint8_t id);
uint8_t header_name_to_id(const char *name);

void header_list_init(struct bhttp_header_list *list);
void header_list_free(struct bhttp_header_list *list);
int header_list_add(struct bhttp_header_list *list, const char *name, const char *value);
int header_list_add_binary(struct bhttp_header_list *list, uint8_t id, const char *name, uint16_t name_len, const char *val, uint16_t val_len);
const char *header_list_get(const struct bhttp_header_list *list, const char *name);

ssize_t headers_encode(uint8_t *buf, size_t max_len, const struct bhttp_header_list *list);
int headers_decode(struct bhttp_header_list *list, const uint8_t *buf, size_t buf_len, size_t *consumed_bytes);

#endif /* BHTTP_HEADERS_H */
