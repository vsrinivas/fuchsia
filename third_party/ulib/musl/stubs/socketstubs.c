#define _GNU_SOURCE
#include <sys/socket.h>

#include <errno.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "libc.h"

static int stub_socket(int domain, int type, int protocol) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_socket, socket);

static int stub_socketpair(int domain, int type, int protocol, int fd[2]) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_socketpair, socketpair);

static int stub_shutdown(int fd, int how) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_shutdown, shutdown);

static int stub_bind(int fd, const struct sockaddr* addr, socklen_t len) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_bind, bind);

static int stub_connect(int fd, const struct sockaddr* addr, socklen_t len) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_connect, connect);

static int stub_listen(int fd, int backlog) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_listen, listen);

static int stub_accept4(int fd, struct sockaddr* restrict addr, socklen_t* restrict len, int flags) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_accept4, accept4);

static int stub_getsockname(int fd, struct sockaddr* restrict addr, socklen_t* restrict len) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_getsockname, getsockname);

static int stub_getpeername(int fd, struct sockaddr* restrict addr, socklen_t* restrict len) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_getpeername, getpeername);

static ssize_t stub_sendto(int fd, const void* buf, size_t buflen, int flags, const struct sockaddr* addr, socklen_t addrlen) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_sendto, sendto);

static ssize_t stub_recvfrom(int fd, void* restrict buf, size_t buflen, int flags, struct sockaddr* restrict addr, socklen_t* restrict addrlen) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_recvfrom, recvfrom);

static ssize_t stub_sendmsg(int fd, const struct msghdr* msg, int flags) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_sendmsg, sendmsg);

static ssize_t stub_recvmsg(int fd, struct msghdr* msg, int flags) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_recvmsg, recvmsg);

static int stub_sendmmsg(int fd, struct mmsghdr* msgvec, unsigned int vlen, unsigned int flags) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_sendmmsg, sendmmsg);

static int stub_recvmmsg(int fd, struct mmsghdr* msgvec, unsigned int vlen, unsigned int flags, struct timespec* timeout) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_recvmmsg, recvmmsg);

static int stub_getsockopt(int fd, int level, int optname, void* restrict optval, socklen_t* restrict optlen) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_getsockopt, getsockopt);

static int stub_setsockopt(int fd, int level, int optname, const void* optval, socklen_t optlen) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_setsockopt, setsockopt);

static int stub_sockatmark(int fd) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_sockatmark, sockatmark);

static int stub_getaddrinfo(const char* restrict host, const char* restrict serv,
                            const struct addrinfo* restrict hint, struct addrinfo** restrict res) {
    errno = ENOSYS;
    return EAI_SYSTEM;
}
weak_alias(stub_getaddrinfo, getaddrinfo);

static void stub_freeaddrinfo(struct addrinfo* p) {
}
weak_alias(stub_freeaddrinfo, freeaddrinfo);
