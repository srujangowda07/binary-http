#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <fcntl.h>

#include <limits.h>

#include "protocol.h"
#include "frame.h"
#include "headers.h"
#include "net.h"
#include "util.h"

static int send_error_response(int fd, uint32_t stream_id, int status, const char *msg) {
    struct bhttp_response res;
    bhttp_response_init(&res);

    res.stream_id = stream_id;
    res.status = status;
    res.body = (uint8_t *)strdup(msg);
    res.body_len = strlen(msg);

    char len_str[32];
    snprintf(len_str, sizeof(len_str), "%zu", res.body_len);

    header_list_add(&res.headers, "content-type", "text/plain");
    header_list_add(&res.headers, "content-length", len_str);
    header_list_add(&res.headers, "server", "observe/1.0");
    header_list_add(&res.headers, "connection", "keep-alive");

    uint8_t frame_buf[1024];
    ssize_t encoded_len = bhttp_encode_response(frame_buf, sizeof(frame_buf), &res);
    bhttp_response_free(&res);

    if (encoded_len < 0) {
        return -1;
    }

    return net_write_all(fd, frame_buf, (size_t)encoded_len) < 0 ? -1 : 0;
}

static void handle_client(int client_fd, const char *docroot) {
    uint8_t hdr_buf[BHTTP_HEADER_SIZE];

    /* Set 5-second socket timeout so slow/stalled clients don't hang the server */
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, (const void *)&tv, sizeof(tv));

    /* Persistent connection loop: process sequential frames until client disconnects */
    while (1) {
        ssize_t r = net_read_exact(client_fd, hdr_buf, BHTTP_HEADER_SIZE);
        if (r == 0) {
            /* Clean connection close by client */
            break;
        }
        if (r < 0) {
            /* Unexpected EOF or read error */
            break;
        }

        /* Detect standard text HTTP (e.g. browser sending "GET ", "POST", "HEAD") */
        if (memcmp(hdr_buf, "GET ", 4) == 0 || memcmp(hdr_buf, "POST", 4) == 0 ||
            memcmp(hdr_buf, "HEAD", 4) == 0 || memcmp(hdr_buf, "HTTP", 4) == 0) {
            fprintf(stderr, "[observe] Notice: Text HTTP request detected on binary port. Rejecting.\n");
            send_error_response(client_fd, 0, STATUS_BAD_REQUEST, "400 Bad Request: Plain text HTTP is not supported. Use bcurl.\n");
            break;
        }


        struct bhttp_frame_header fhdr;
        if (frame_header_decode(&fhdr, hdr_buf) < 0) {
            send_error_response(client_fd, 0, STATUS_BAD_REQUEST, "400 Bad Request: Invalid Frame Header\n");
            break;
        }

        if (fhdr.length > BHTTP_MAX_PAYLOAD_SIZE) {
            send_error_response(client_fd, fhdr.stream_id, STATUS_BAD_REQUEST, "400 Bad Request: Frame Size Exceeded\n");
            break;
        }

        /* Unknown Frame Handling: MUST skip unknown frame types cleanly */
        if (fhdr.type != FRAME_TYPE_REQUEST) {
            fprintf(stderr, "[observe] Notice: Unknown frame type 0x%02x (length %u bytes) on stream %u. Skipping cleanly.\n",
                    fhdr.type, fhdr.length, fhdr.stream_id);
            if (net_discard_exact(client_fd, fhdr.length) < 0) {
                break;
            }
            continue;
        }

        /* Read request payload */
        uint8_t *payload_buf = NULL;
        if (fhdr.length > 0) {
            payload_buf = malloc(fhdr.length);
            if (!payload_buf) {
                send_error_response(client_fd, fhdr.stream_id, STATUS_SERVER_ERROR, "500 Internal Server Error: Out of Memory\n");
                break;
            }
            if (net_read_exact(client_fd, payload_buf, fhdr.length) != (ssize_t)fhdr.length) {
                free(payload_buf);
                break;
            }
        }

        struct bhttp_request req;
        if (bhttp_decode_request_payload(&req, fhdr.stream_id, payload_buf, fhdr.length) < 0) {
            if (payload_buf) free(payload_buf);
            send_error_response(client_fd, fhdr.stream_id, STATUS_BAD_REQUEST, "400 Bad Request: Malformed Payload\n");
            continue;
        }
        if (payload_buf) free(payload_buf);

        if (strcmp(req.method, "GET") != 0) {
            send_error_response(client_fd, req.stream_id, STATUS_BAD_REQUEST, "400 Bad Request: Unsupported Method\n");
            bhttp_request_free(&req);
            continue;
        }

        char safe_path[PATH_MAX];
        int path_res = resolve_safe_path(docroot, req.path, safe_path, sizeof(safe_path));

        if (path_res == -1) {
            /* Traversal attempt or invalid path syntax */
            send_error_response(client_fd, req.stream_id, STATUS_BAD_REQUEST, "400 Bad Request: Path Traversal Rejected\n");
            bhttp_request_free(&req);
            continue;
        } else if (path_res == -2) {
            /* File does not exist */
            send_error_response(client_fd, req.stream_id, STATUS_NOT_FOUND, "404 Not Found: Resource Does Not Exist\n");
            bhttp_request_free(&req);
            continue;
        } else if (path_res == -3) {
            /* Internal system error */
            send_error_response(client_fd, req.stream_id, STATUS_SERVER_ERROR, "500 Internal Server Error: Filesystem Error\n");
            bhttp_request_free(&req);
            continue;
        }

        /* Open and read the target file */
        FILE *fp = fopen(safe_path, "rb");
        if (!fp) {
            send_error_response(client_fd, req.stream_id, STATUS_NOT_FOUND, "404 Not Found: Cannot Open File\n");
            bhttp_request_free(&req);
            continue;
        }

        fseek(fp, 0, SEEK_END);
        long fsize = ftell(fp);
        fseek(fp, 0, SEEK_SET);

        if (fsize < 0 || (size_t)fsize > BHTTP_MAX_PAYLOAD_SIZE - 1024) {
            fclose(fp);
            send_error_response(client_fd, req.stream_id, STATUS_SERVER_ERROR, "500 Internal Server Error: File Too Large\n");
            bhttp_request_free(&req);
            continue;
        }

        uint8_t *file_data = NULL;
        if (fsize > 0) {
            file_data = malloc((size_t)fsize);
            if (!file_data) {
                fclose(fp);
                send_error_response(client_fd, req.stream_id, STATUS_SERVER_ERROR, "500 Internal Server Error: Out of Memory\n");
                bhttp_request_free(&req);
                continue;
            }
            if (fread(file_data, 1, (size_t)fsize, fp) != (size_t)fsize) {
                free(file_data);
                fclose(fp);
                send_error_response(client_fd, req.stream_id, STATUS_SERVER_ERROR, "500 Internal Server Error: Read Failed\n");
                bhttp_request_free(&req);
                continue;
            }
        }
        fclose(fp);

        struct bhttp_response res;
        bhttp_response_init(&res);
        res.stream_id = req.stream_id;
        res.status = STATUS_OK;
        res.body = file_data;
        res.body_len = (size_t)fsize;

        char len_str[32];
        snprintf(len_str, sizeof(len_str), "%ld", fsize);

        header_list_add(&res.headers, "content-type", mime_type_from_path(safe_path));
        header_list_add(&res.headers, "content-length", len_str);
        header_list_add(&res.headers, "server", "observe/1.0");
        header_list_add(&res.headers, "connection", "keep-alive");

        size_t resp_buf_size = BHTTP_HEADER_SIZE + 2048 + (size_t)fsize;
        uint8_t *resp_buf = malloc(resp_buf_size);
        if (!resp_buf) {
            bhttp_response_free(&res);
            bhttp_request_free(&req);
            send_error_response(client_fd, req.stream_id, STATUS_SERVER_ERROR, "500 Internal Server Error: Out of Memory\n");
            continue;
        }

        ssize_t encoded_resp = bhttp_encode_response(resp_buf, resp_buf_size, &res);
        if (encoded_resp > 0) {
            net_write_all(client_fd, resp_buf, (size_t)encoded_resp);
        }

        free(resp_buf);
        bhttp_response_free(&res);
        bhttp_request_free(&req);
    }

    close(client_fd);
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <document_root> <port>\n", argv[0]);
        return 1;
    }

    const char *docroot = argv[1];
    const char *port = argv[2];

    struct stat st;
    if (stat(docroot, &st) != 0 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "Error: Document root '%s' does not exist or is not a directory\n", docroot);
        return 1;
    }

    int listen_fd = net_listen(port, 16);
    if (listen_fd < 0) {
        fprintf(stderr, "Error: Failed to bind and listen on port %s\n", port);
        return 1;
    }

    fprintf(stderr, "[observe] Serving %s on port %s\n", docroot, port);

    while (1) {
        int client_fd = accept(listen_fd, NULL, NULL);
        if (client_fd < 0) {
            continue;
        }
        handle_client(client_fd, docroot);
    }

    close(listen_fd);
    return 0;
}
