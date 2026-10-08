#ifndef BHTTP_NET_H
#define BHTTP_NET_H

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


ssize_t net_read_exact(int fd, void *buf, size_t len);
ssize_t net_write_all(int fd, const void *buf, size_t len);
int net_discard_exact(int fd, size_t len);

int net_listen(const char *port, int backlog);
int net_connect(const char *host, const char *port);

#endif /* BHTTP_NET_H */
