#include "net.h"
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

ssize_t net_read_exact(int fd, void *buf, size_t len) {
    if (!buf && len > 0) return -1;
    size_t total = 0;
    char *p = (char *)buf;

    while (total < len) {
        ssize_t r = recv(fd, p + total, len - total, 0);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) {
            /* Connection closed by peer */
            return (total == 0) ? 0 : -2;
        }
        total += (size_t)r;
    }
    return (ssize_t)total;
}

ssize_t net_write_all(int fd, const void *buf, size_t len) {
    if (!buf && len > 0) return -1;
    size_t total = 0;
    const char *p = (const char *)buf;

    while (total < len) {
        ssize_t w = send(fd, p + total, len - total, 0);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        total += (size_t)w;
    }
    return (ssize_t)total;
}

int net_discard_exact(int fd, size_t len) {
    char scratch[4096];
    size_t remaining = len;

    while (remaining > 0) {
        size_t chunk = remaining < sizeof(scratch) ? remaining : sizeof(scratch);
        ssize_t r = net_read_exact(fd, scratch, chunk);
        if (r != (ssize_t)chunk) {
            return -1;
        }
        remaining -= chunk;
    }
    return 0;
}

int net_listen(const char *port, int backlog) {
    struct addrinfo hints, *res, *rp;
    int sfd = -1;
    int optval = 1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, port, &hints, &res) != 0) {
        return -1;
    }

    for (rp = res; rp != NULL; rp = rp->ai_next) {
        sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sfd < 0) continue;

        setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

        if (bind(sfd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }
        close(sfd);
        sfd = -1;
    }

    freeaddrinfo(res);

    if (sfd < 0) return -1;

    if (listen(sfd, backlog) < 0) {
        close(sfd);
        return -1;
    }

    return sfd;
}

int net_connect(const char *host, const char *port) {
    struct addrinfo hints, *res, *rp;
    int sfd = -1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, port, &hints, &res) != 0) {
        return -1;
    }

    for (rp = res; rp != NULL; rp = rp->ai_next) {
        sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sfd < 0) continue;

        if (connect(sfd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }
        close(sfd);
        sfd = -1;
    }

    freeaddrinfo(res);
    return sfd;
}
