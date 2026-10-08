#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "protocol.h"
#include "frame.h"
#include "headers.h"
#include "net.h"
#include "util.h"

int main(int argc, char **argv) {
    int verbose = 0;
    const char *url_arg = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0) {
            verbose = 1;
        } else if (!url_arg) {
            url_arg = argv[i];
        } else {
            fprintf(stderr, "Error: Unexpected argument '%s'\n", argv[i]);
            return 1;
        }
    }

    if (!url_arg) {
        fprintf(stderr, "Usage: %s [-v] <host:port/path>\n", argv[0]);
        return 1;
    }

    char host[256];
    char port[32];
    char path[1024];
    if (parse_url(url_arg, host, sizeof(host), port, sizeof(port), path, sizeof(path)) < 0) {
        fprintf(stderr, "Error: Failed to parse URL '%s'\n", url_arg);
        return 1;
    }

    if (verbose) {
        fprintf(stderr, "* Connecting to %s port %s...\n", host, port);
    }

    int sock = net_connect(host, port);
    if (sock < 0) {
        fprintf(stderr, "Error: Could not connect to %s:%s\n", host, port);
        return 1;
    }

    if (verbose) {
        fprintf(stderr, "* Connected successfully.\n");
    }

    /* Build request */
    struct bhttp_request req;
    bhttp_request_init(&req);
    req.stream_id = 1;
    req.method = strdup("GET");
    req.path = strdup(path);

    char host_hdr[300];
    snprintf(host_hdr, sizeof(host_hdr), "%s:%s", host, port);
    header_list_add(&req.headers, "host", host_hdr);
    header_list_add(&req.headers, "user-agent", "bcurl/1.0");
    header_list_add(&req.headers, "accept", "*/*");

    uint8_t req_buf[4096];
    ssize_t req_len = bhttp_encode_request(req_buf, sizeof(req_buf), &req);
    if (req_len < 0) {
        fprintf(stderr, "Error: Failed to encode request frame\n");
        bhttp_request_free(&req);
        close(sock);
        return 1;
    }

    if (verbose) {
        fprintf(stderr, "> Transmitting FRAME_REQUEST (%zd bytes, Stream ID %u):\n", req_len, req.stream_id);
        hexdump_print(stderr, req_buf, (size_t)req_len, ">");
    }

    if (net_write_all(sock, req_buf, (size_t)req_len) < 0) {
        fprintf(stderr, "Error: Failed to send request frame\n");
        bhttp_request_free(&req);
        close(sock);
        return 1;
    }
    bhttp_request_free(&req);

    /* Read response frame(s) */
    int exit_code = 0;
    while (1) {
        uint8_t hdr_buf[BHTTP_HEADER_SIZE];
        ssize_t r = net_read_exact(sock, hdr_buf, BHTTP_HEADER_SIZE);
        if (r <= 0) {
            fprintf(stderr, "Error: Connection closed before complete response received\n");
            close(sock);
            return 1;
        }

        struct bhttp_frame_header fhdr;
        if (frame_header_decode(&fhdr, hdr_buf) < 0) {
            fprintf(stderr, "Error: Corrupted response frame header\n");
            close(sock);
            return 1;
        }

        /* Check for unknown frame type and skip payload cleanly per protocol spec */
        if (fhdr.type != FRAME_TYPE_RESPONSE) {
            if (verbose) {
                fprintf(stderr, "* Unknown frame type 0x%02x (len %u). Skipping payload cleanly.\n",
                        fhdr.type, fhdr.length);
            }
            if (net_discard_exact(sock, fhdr.length) < 0) {
                fprintf(stderr, "Error: Failed to skip unknown frame payload\n");
                close(sock);
                return 1;
            }
            continue;
        }

        /* Read expected response payload */
        uint8_t *payload_buf = NULL;
        if (fhdr.length > 0) {
            payload_buf = malloc(fhdr.length);
            if (!payload_buf) {
                fprintf(stderr, "Error: Out of memory for response payload\n");
                close(sock);
                return 1;
            }
            if (net_read_exact(sock, payload_buf, fhdr.length) != (ssize_t)fhdr.length) {
                fprintf(stderr, "Error: Incomplete response payload received\n");
                free(payload_buf);
                close(sock);
                return 1;
            }
        }

        if (verbose) {
            fprintf(stderr, "< Received FRAME_RESPONSE (Length: %u, Stream ID: %u):\n", fhdr.length, fhdr.stream_id);
            /* Print complete frame hexdump (header + payload) */
            uint8_t *full_frame = malloc(BHTTP_HEADER_SIZE + fhdr.length);
            if (full_frame) {
                memcpy(full_frame, hdr_buf, BHTTP_HEADER_SIZE);
                if (fhdr.length > 0 && payload_buf) {
                    memcpy(full_frame + BHTTP_HEADER_SIZE, payload_buf, fhdr.length);
                }
                hexdump_print(stderr, full_frame, BHTTP_HEADER_SIZE + fhdr.length, "<");
                free(full_frame);
            }
        }

        struct bhttp_response res;
        if (bhttp_decode_response_payload(&res, fhdr.stream_id, payload_buf, fhdr.length) < 0) {
            fprintf(stderr, "Error: Failed to decode response payload\n");
            if (payload_buf) free(payload_buf);
            close(sock);
            return 1;
        }
        if (payload_buf) free(payload_buf);

        if (verbose) {
            fprintf(stderr, "< Status: %d\n", res.status);
            for (size_t i = 0; i < res.headers.count; i++) {
                fprintf(stderr, "< %s: %s\n", res.headers.items[i].name, res.headers.items[i].value);
            }
        }

        /* Write body to stdout */
        if (res.body_len > 0 && res.body) {
            fwrite(res.body, 1, res.body_len, stdout);
            fflush(stdout);
        }

        if (res.status >= 400) {
            exit_code = (res.status >= 500) ? 2 : 1;
        } else {
            exit_code = 0;
        }

        bhttp_response_free(&res);
        break;
    }

    close(sock);
    return exit_code;
}
