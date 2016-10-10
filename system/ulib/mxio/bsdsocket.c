// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include <sys/socket.h>
#include <netdb.h>

#include <mxio/debug.h>
#include <mxio/io.h>
#include <mxio/remoteio.h>
#include <mxio/util.h>
#include <mxio/socket.h>

#include "unistd.h"

int socket(int domain, int type, int protocol) {
    mxio_t* io = NULL;
    mx_status_t r;

    char path[1024];
    int n = snprintf(path, sizeof(path), "%s/%s/%d/%d/%d", MXRIO_SOCKET_ROOT,
                     MXRIO_SOCKET_DIR_SOCKET, domain, type, protocol);
    if (n < 0 || n >= (int)sizeof(path)) {
        return ERRNO(EINVAL);
    }

    if ((r = __mxio_open(&io, path, 0, 0)) < 0) {
        return ERROR(r);
    }

    int fd;
    if ((fd = mxio_bind_to_fd(io, -1, 0)) < 0) {
        io->ops->close(io);
        mxio_release(io);
        return ERRNO(EMFILE);
    }
    return fd;
}

int connect(int fd, const struct sockaddr* addr, socklen_t len) {
    mxio_t* io = fd_to_io(fd);
    if (io == NULL) {
        return ERRNO(EBADF);
    }

    mx_status_t r;
    r = io->ops->misc(io, MXRIO_CONNECT, 0, 0, (void*)addr, len);
    mxio_release(io);
    return STATUS(r);
}

int bind(int fd, const struct sockaddr* addr, socklen_t len) {
    mxio_t* io = fd_to_io(fd);
    if (io == NULL) {
        return ERRNO(EBADF);
    }

    mx_status_t r;
    r = io->ops->misc(io, MXRIO_BIND, 0, 0, (void*)addr, len);
    mxio_release(io);
    return STATUS(r);
}

int listen(int fd, int backlog) {
    mxio_t* io = fd_to_io(fd);
    if (io == NULL) {
        return ERRNO(EBADF);
    }

    mx_status_t r;
    r = io->ops->misc(io, MXRIO_LISTEN, 0, 0, &backlog, sizeof(backlog));
    mxio_release(io);
    return STATUS(r);
}

int accept4(int fd, struct sockaddr* restrict addr, socklen_t* restrict len,
            int flags) {
    // TODO: support flags

    mxio_t* io = fd_to_io(fd);
    if (io == NULL) {
        return ERRNO(EBADF);
    }

    mxio_t* io2;
    mx_status_t r = io->ops->open(io, MXRIO_SOCKET_DIR_ACCEPT, 0, 0, &io2);
    mxio_release(io);
    if (r < 0) {
        return ERROR(r);
    }

    if (addr != NULL && len != NULL) {
        mxrio_sockaddr_reply_t reply;
        if ((r = io2->ops->misc(io2, MXRIO_GETPEERNAME, 0,
                                sizeof(mxrio_sockaddr_reply_t), &reply,
                                sizeof(reply))) < 0) {
            io->ops->close(io2);
            mxio_release(io2);
            return ERROR(r);
        }
        socklen_t avail = *len;
        *len = reply.len;
        memcpy(addr, &reply.addr, (avail < reply.len) ? avail : reply.len);
    }

    int fd2;
    if ((fd2 = mxio_bind_to_fd(io2, -1, 0)) < 0) {
        io->ops->close(io2);
        mxio_release(io2);
        return ERRNO(EMFILE);
    }
    return fd2;
}

int getaddrinfo(const char* __restrict node,
                const char* __restrict service,
                const struct addrinfo* __restrict hints,
                struct addrinfo** __restrict res) {
    mxio_t* io = NULL;
    mx_status_t r;

    if ((node == NULL && service == NULL) || res == NULL) {
        return ERRNO(EINVAL);
    }
    if ((r = __mxio_open(&io, MXRIO_SOCKET_ROOT "/" MXRIO_SOCKET_DIR_NONE,
                         0, 0)) < 0) {
        return ERROR(r);
    }

    static_assert(sizeof(mxrio_gai_req_t) >= sizeof(mxrio_gai_reply_t),
                  "this code assumes req is larger than or equal to reply");

    mxrio_gai_req_t req;

    req.node_is_null = (node == NULL) ? 1 : 0;
    req.service_is_null = (service == NULL) ? 1 : 0;
    req.hints_is_null = (hints == NULL) ? 1 : 0;
    if (node) {
        strncpy(req.node, node, MXRIO_GAI_REQ_NODE_MAXLEN);
        req.node[MXRIO_GAI_REQ_NODE_MAXLEN-1] = '\0';
    }
    if (service) {
        strncpy(req.service, service, MXRIO_GAI_REQ_SERVICE_MAXLEN);
        req.service[MXRIO_GAI_REQ_SERVICE_MAXLEN-1] = '\0';
    }
    if (hints) {
        if (hints->ai_addrlen != 0 || hints->ai_addr != NULL ||
            hints->ai_canonname != NULL || hints->ai_next != NULL) {
            return ERRNO(EINVAL);
        }
        memcpy(&req.hints, hints, sizeof(struct addrinfo));
    }

    r = io->ops->misc(io, MXRIO_GETADDRINFO, 0, sizeof(mxrio_gai_reply_t),
                      &req, sizeof(mxrio_gai_req_t));
    io->ops->close(io);
    mxio_release(io);

    if (r < 0) {
        return ERROR(r);
    }

    // alloc the memory for the out param
    mxrio_gai_reply_t* reply = calloc(1, sizeof(mxrio_gai_reply_t));
    // copy the reply
    memcpy(reply, &req, sizeof(mxrio_gai_reply_t));

    // link all entries in the reply
    struct addrinfo *next = NULL;
    for (int i = reply->nres - 1; i >= 0; --i) {
        reply->res[i].ai.ai_addr = (struct sockaddr*)&reply->res[i].addr;
        reply->res[i].ai.ai_next = next;
        next = &reply->res[i].ai;
    }
    // the top of the reply must be the first addrinfo in the list
    assert(next == (struct addrinfo*)reply);
    *res = next;

    return 0;
}

void freeaddrinfo(struct addrinfo* res) {
    free(res);
}

static int getsockaddr(int fd, int op, struct sockaddr* restrict addr,
                       socklen_t* restrict len) {
    if (len == NULL || addr == NULL) {
        return ERRNO(EINVAL);
    }

    mxio_t* io = fd_to_io(fd);
    if (io == NULL) {
        return ERRNO(EBADF);
    }

    mxrio_sockaddr_reply_t reply;
    mx_status_t r = io->ops->misc(io, op, 0, sizeof(mxrio_sockaddr_reply_t),
                                  &reply, sizeof(reply));
    mxio_release(io);

    if (r < 0) {
        return ERROR(r);
    }

    socklen_t avail = *len;
    *len = reply.len;
    memcpy(addr, &reply.addr, (avail < reply.len) ? avail : reply.len);

    return 0;
}

int getsockname(int fd, struct sockaddr* restrict addr, socklen_t* restrict len)
{
    return getsockaddr(fd, MXRIO_GETSOCKNAME, addr, len);
}

int getpeername(int fd, struct sockaddr* restrict addr, socklen_t* restrict len)
{
    return getsockaddr(fd, MXRIO_GETPEERNAME, addr, len);
}

int getsockopt(int fd, int level, int optname, void* restrict optval,
               socklen_t* restrict optlen) {
    if (optval == NULL || optlen == NULL) {
        return ERRNO(EINVAL);
    }

    mxio_t* io = fd_to_io(fd);
    if (io == NULL) {
        return ERRNO(EBADF);
    }

    mxrio_sockopt_req_reply_t reply;
    mx_status_t r = io->ops->misc(io, MXRIO_GETSOCKOPT, 0,
                                  sizeof(mxrio_sockopt_req_reply_t),
                                  &reply, sizeof(reply));
    mxio_release(io);

    if (r < 0) {
        return ERROR(r);
    }

    socklen_t avail = *optlen;
    *optlen = reply.optlen;
    memcpy(optval, reply.optval, (avail < reply.optlen) ? avail : reply.optlen);

    return 0;
}

int setsockopt(int fd, int level, int optname, const void* optval,
               socklen_t optlen) {
    mxio_t* io = fd_to_io(fd);
    if (io == NULL) {
        return ERRNO(EBADF);
    }

    mxrio_sockopt_req_reply_t req;
    req.level = level;
    req.optname = optname;
    if (optlen > sizeof(req.optval)) {
        io->ops->close(io);
        mxio_release(io);
        return ERRNO(EINVAL);
    }
    req.optlen = optlen;
    mx_status_t r = io->ops->misc(io, MXRIO_SETSOCKOPT, 0, 0, &req,
                                  sizeof(req));
    mxio_release(io);
    return STATUS(r);
}
