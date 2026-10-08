#ifndef BHTTP_UTIL_H
#define BHTTP_UTIL_H

#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

void hexdump_print(FILE *out, const void *data, size_t len, const char *prefix);
int resolve_safe_path(const char *docroot, const char *url_path, char *out_path, size_t out_len);
const char *mime_type_from_path(const char *path);
int parse_url(const char *url, char *host, size_t host_len, char *port, size_t port_len, char *path, size_t path_len);

#endif /* BHTTP_UTIL_H */
