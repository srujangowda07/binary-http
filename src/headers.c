#include "protocol.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char *const STATIC_TABLE[STATIC_TABLE_SIZE + 1] = {
    NULL,             /* 0 is reserved for literal/custom */
    ":method",        /* 1 */
    ":path",          /* 2 */
    ":status",        /* 3 */
    "content-length", /* 4 */
    "content-type",   /* 5 */
    "host",           /* 6 */
    "user-agent",     /* 7 */
    "server",         /* 8 */
    "accept",         /* 9 */
    "connection"      /* 10 */
};

const char *header_id_to_name(uint8_t id) {
    if (id >= 1 && id <= STATIC_TABLE_SIZE) {
        return STATIC_TABLE[id];
    }
    return NULL;
}

uint8_t header_name_to_id(const char *name) {
    if (!name) return HEADER_ID_LITERAL;
    for (uint8_t i = 1; i <= STATIC_TABLE_SIZE; i++) {
        if (strcasecmp(name, STATIC_TABLE[i]) == 0) {
            return i;
        }
    }
    return HEADER_ID_LITERAL;
}

void header_list_init(struct bhttp_header_list *list) {
    if (!list) return;
    memset(list, 0, sizeof(*list));
}

void header_list_free(struct bhttp_header_list *list) {
    if (!list) return;
    for (size_t i = 0; i < list->count; i++) {
        if (list->items[i].id == HEADER_ID_LITERAL && list->items[i].name) {
            free(list->items[i].name);
        }
        if (list->items[i].value) {
            free(list->items[i].value);
        }
    }
    list->count = 0;
}

int header_list_add(struct bhttp_header_list *list, const char *name, const char *value) {
    if (!list || !name || !value) return -1;
    if (list->count >= BHTTP_MAX_HEADER_COUNT) return -1;

    size_t nlen = strlen(name);
    size_t vlen = strlen(value);
    if (nlen > BHTTP_MAX_HEADER_NAME_LEN || vlen > BHTTP_MAX_HEADER_VAL_LEN) return -1;

    uint8_t id = header_name_to_id(name);
    struct bhttp_header *h = &list->items[list->count];
    h->id = id;
    h->name_len = (uint16_t)nlen;
    h->value_len = (uint16_t)vlen;

    if (id == HEADER_ID_LITERAL) {
        h->name = malloc(nlen + 1);
        if (!h->name) return -1;
        memcpy(h->name, name, nlen);
        h->name[nlen] = '\0';
    } else {
        h->name = (char *)STATIC_TABLE[id];
    }

    h->value = malloc(vlen + 1);
    if (!h->value) {
        if (id == HEADER_ID_LITERAL) free(h->name);
        return -1;
    }
    memcpy(h->value, value, vlen);
    h->value[vlen] = '\0';

    list->count++;
    return 0;
}

int header_list_add_binary(struct bhttp_header_list *list, uint8_t id, const char *name, uint16_t name_len, const char *val, uint16_t val_len) {
    if (!list || !val) return -1;
    if (list->count >= BHTTP_MAX_HEADER_COUNT) return -1;

    struct bhttp_header *h = &list->items[list->count];
    h->id = id;
    h->name_len = name_len;
    h->value_len = val_len;

    if (id >= 1 && id <= STATIC_TABLE_SIZE) {
        h->name = (char *)STATIC_TABLE[id];
    } else {
        h->id = HEADER_ID_LITERAL;
        if (!name) return -1;
        h->name = malloc((size_t)name_len + 1);
        if (!h->name) return -1;
        memcpy(h->name, name, name_len);
        h->name[name_len] = '\0';
    }

    h->value = malloc((size_t)val_len + 1);
    if (!h->value) {
        if (h->id == HEADER_ID_LITERAL) free(h->name);
        return -1;
    }
    memcpy(h->value, val, val_len);
    h->value[val_len] = '\0';

    list->count++;
    return 0;
}

const char *header_list_get(const struct bhttp_header_list *list, const char *name) {
    if (!list || !name) return NULL;
    for (size_t i = 0; i < list->count; i++) {
        if (list->items[i].name && strcasecmp(list->items[i].name, name) == 0) {
            return list->items[i].value;
        }
    }
    return NULL;
}

ssize_t headers_encode(uint8_t *buf, size_t max_len, const struct bhttp_header_list *list) {
    if (!buf || !list) return -1;
    if (max_len < 2) return -1;

    size_t offset = 0;
    encode_u16(buf + offset, (uint16_t)list->count);
    offset += 2;

    for (size_t i = 0; i < list->count; i++) {
        const struct bhttp_header *h = &list->items[i];
        if (h->id >= 1 && h->id <= STATIC_TABLE_SIZE) {
            /* 1 byte ID + 2 bytes value_len + value */
            size_t needed = 1 + 2 + h->value_len;
            if (offset + needed > max_len) return -1;
            buf[offset++] = h->id;
            encode_u16(buf + offset, h->value_len);
            offset += 2;
            if (h->value_len > 0 && h->value) {
                memcpy(buf + offset, h->value, h->value_len);
                offset += h->value_len;
            }
        } else {
            /* Literal: 1 byte 0x00 + 2 bytes name_len + name + 2 bytes val_len + val */
            size_t needed = 1 + 2 + h->name_len + 2 + h->value_len;
            if (offset + needed > max_len) return -1;
            buf[offset++] = HEADER_ID_LITERAL;
            encode_u16(buf + offset, h->name_len);
            offset += 2;
            if (h->name_len > 0 && h->name) {
                memcpy(buf + offset, h->name, h->name_len);
                offset += h->name_len;
            }
            encode_u16(buf + offset, h->value_len);
            offset += 2;
            if (h->value_len > 0 && h->value) {
                memcpy(buf + offset, h->value, h->value_len);
                offset += h->value_len;
            }
        }
    }

    return (ssize_t)offset;
}

int headers_decode(struct bhttp_header_list *list, const uint8_t *buf, size_t buf_len, size_t *consumed_bytes) {
    if (!list || !buf || !consumed_bytes) return -1;
    if (buf_len < 2) return -1;

    header_list_init(list);

    size_t offset = 0;
    uint16_t num_headers = decode_u16(buf + offset);
    offset += 2;

    if (num_headers > BHTTP_MAX_HEADER_COUNT) return -1;

    for (uint16_t i = 0; i < num_headers; i++) {
        if (offset >= buf_len) {
            header_list_free(list);
            return -1;
        }

        uint8_t id = buf[offset++];
        if (id >= 1 && id <= STATIC_TABLE_SIZE) {
            if (offset + 2 > buf_len) {
                header_list_free(list);
                return -1;
            }
            uint16_t vlen = decode_u16(buf + offset);
            offset += 2;
            if (offset + vlen > buf_len) {
                header_list_free(list);
                return -1;
            }
            if (header_list_add_binary(list, id, NULL, 0, (const char *)(buf + offset), vlen) < 0) {
                header_list_free(list);
                return -1;
            }
            offset += vlen;
        } else if (id == HEADER_ID_LITERAL) {
            if (offset + 2 > buf_len) {
                header_list_free(list);
                return -1;
            }
            uint16_t nlen = decode_u16(buf + offset);
            offset += 2;
            if (offset + nlen > buf_len) {
                header_list_free(list);
                return -1;
            }
            const char *name_ptr = (const char *)(buf + offset);
            offset += nlen;

            if (offset + 2 > buf_len) {
                header_list_free(list);
                return -1;
            }
            uint16_t vlen = decode_u16(buf + offset);
            offset += 2;
            if (offset + vlen > buf_len) {
                header_list_free(list);
                return -1;
            }
            const char *val_ptr = (const char *)(buf + offset);
            offset += vlen;

            if (header_list_add_binary(list, id, name_ptr, nlen, val_ptr, vlen) < 0) {
                header_list_free(list);
                return -1;
            }
        } else {
            /* Invalid header ID */
            header_list_free(list);
            return -1;
        }
    }

    *consumed_bytes = offset;
    return 0;
}
