#include "util.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

void hexdump_print(FILE *out, const void *data, size_t len, const char *prefix) {
    if (!out || !data) return;
    const uint8_t *p = (const uint8_t *)data;
    char pfx[64];
    if (prefix) {
        snprintf(pfx, sizeof(pfx), "%s ", prefix);
    } else {
        pfx[0] = '\0';
    }

    for (size_t i = 0; i < len; i += 16) {
        fprintf(out, "%s%04zx  ", pfx, i);

        /* Print hex bytes */
        for (size_t j = 0; j < 16; j++) {
            if (i + j < len) {
                fprintf(out, "%02x ", p[i + j]);
            } else {
                fprintf(out, "   ");
            }
            if (j == 7) fprintf(out, " ");
        }

        fprintf(out, " |");
        /* Print ASCII representation */
        for (size_t j = 0; j < 16 && (i + j) < len; j++) {
            uint8_t c = p[i + j];
            fputc(isprint(c) ? c : '.', out);
        }
        fprintf(out, "|\n");
    }
}

int resolve_safe_path(const char *docroot, const char *url_path, char *out_path, size_t out_len) {
    if (!docroot || !url_path || !out_path || out_len == 0) {
        return -1;
    }

    /* Reject paths that do not start with '/' */
    if (url_path[0] != '/') {
        return -1;
    }

    /* Reject backslashes or null bytes */
    if (strchr(url_path, '\\')) {
        return -1;
    }

    /* Reject path traversal tokens */
    if (strstr(url_path, "/../") ||
        strcmp(url_path, "/..") == 0 ||
        strstr(url_path, "/..") == (url_path + strlen(url_path) - 3) ||
        strstr(url_path, "//")) {
        return -1;
    }

    /* Check canonical docroot */
    char canonical_root[PATH_MAX];
    if (!realpath(docroot, canonical_root)) {
        return -3;
    }
    size_t root_len = strlen(canonical_root);

    /* Construct candidate path */
    const char *subpath = url_path;
    if (strcmp(subpath, "/") == 0) {
        subpath = "/index.html";
    }

    char candidate[PATH_MAX];
    int n = snprintf(candidate, sizeof(candidate), "%s%s", canonical_root, subpath);
    if (n < 0 || (size_t)n >= sizeof(candidate)) {
        return -1;
    }

    /* Check if target exists on filesystem */
    struct stat st;
    if (stat(candidate, &st) != 0) {
        if (errno == ENOENT) {
            return -2; /* 404 Not Found */
        }
        return -3; /* 500 System Error */
    }

    /* If target is a directory, look for index.html */
    if (S_ISDIR(st.st_mode)) {
        char index_candidate[PATH_MAX];
        int in = snprintf(index_candidate, sizeof(index_candidate), "%s/index.html", candidate);
        if (in < 0 || (size_t)in >= sizeof(index_candidate)) {
            return -1;
        }
        if (stat(index_candidate, &st) != 0 || !S_ISREG(st.st_mode)) {
            return -2; /* 404 Not Found */
        }
        strncpy(candidate, index_candidate, sizeof(candidate) - 1);
        candidate[sizeof(candidate) - 1] = '\0';
    } else if (!S_ISREG(st.st_mode)) {
        return -2; /* Not a regular file */
    }

    /* Canonical verification of final target */
    char canonical_target[PATH_MAX];
    if (!realpath(candidate, canonical_target)) {
        return -2;
    }

    /* Ensure canonical target resides strictly within canonical root */
    if (strncmp(canonical_target, canonical_root, root_len) != 0 ||
        (canonical_target[root_len] != '/' && canonical_target[root_len] != '\0')) {
        return -1; /* Traversal attempt escaped sandbox */
    }

    if (strlen(canonical_target) >= out_len) {
        return -3;
    }

    strncpy(out_path, canonical_target, out_len - 1);
    out_path[out_len - 1] = '\0';
    return 0;
}

const char *mime_type_from_path(const char *path) {
    if (!path) return "application/octet-stream";
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";

    if (strcasecmp(dot, ".html") == 0 || strcasecmp(dot, ".htm") == 0) return "text/html";
    if (strcasecmp(dot, ".txt") == 0) return "text/plain";
    if (strcasecmp(dot, ".json") == 0) return "application/json";
    if (strcasecmp(dot, ".css") == 0) return "text/css";
    if (strcasecmp(dot, ".js") == 0) return "application/javascript";
    if (strcasecmp(dot, ".png") == 0) return "image/png";
    if (strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".jpeg") == 0) return "image/jpeg";
    if (strcasecmp(dot, ".gif") == 0) return "image/gif";
    if (strcasecmp(dot, ".svg") == 0) return "image/svg+xml";

    return "application/octet-stream";
}

int parse_url(const char *url, char *host, size_t host_len, char *port, size_t port_len, char *path, size_t path_len) {
    if (!url || !host || !port || !path) return -1;

    const char *p = url;
    /* Skip optional scheme if provided (e.g. bhttp:// or http://) */
    const char *scheme = strstr(p, "://");
    if (scheme) {
        p = scheme + 3;
    }

    /* Extract host:port portion up to first '/' */
    const char *slash = strchr(p, '/');
    const char *colon = strchr(p, ':');

    size_t host_end = 0;
    if (colon && (!slash || colon < slash)) {
        host_end = (size_t)(colon - p);
        if (host_end >= host_len) return -1;
        memcpy(host, p, host_end);
        host[host_end] = '\0';

        const char *port_start = colon + 1;
        size_t port_cnt = slash ? (size_t)(slash - port_start) : strlen(port_start);
        if (port_cnt >= port_len || port_cnt == 0) return -1;
        memcpy(port, port_start, port_cnt);
        port[port_cnt] = '\0';
    } else {
        /* No port specified, default to 9000 */
        host_end = slash ? (size_t)(slash - p) : strlen(p);
        if (host_end >= host_len || host_end == 0) return -1;
        memcpy(host, p, host_end);
        host[host_end] = '\0';

        strncpy(port, "9000", port_len - 1);
        port[port_len - 1] = '\0';
    }

    if (slash) {
        if (strlen(slash) >= path_len) return -1;
        strncpy(path, slash, path_len - 1);
        path[path_len - 1] = '\0';
    } else {
        strncpy(path, "/", path_len - 1);
        path[path_len - 1] = '\0';
    }

    return 0;
}
