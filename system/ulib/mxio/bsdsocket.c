// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <errno.h>
#include <netdb.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <threads.h>
#include <unistd.h>

#include <magenta/syscalls.h>

#include <mxio/debug.h>
#include <mxio/io.h>
#include <mxio/remoteio.h>
#include <mxio/util.h>
#include <mxio/socket.h>

#include "private.h"
#include "unistd.h"

static mx_status_t mxio_getsockopt(mxio_t* io, int level, int optname,
                                   void* restrict optval,
                                   socklen_t* restrict optlen);

int socket(int domain, int type, int protocol) {
    mxio_t* io = NULL;
    mx_status_t r;

    char path[1024];
    int n = snprintf(path, sizeof(path), "%s/%s/%d/%d/%d", MXRIO_SOCKET_ROOT,
                     MXRIO_SOCKET_DIR_SOCKET, domain, type & ~SOCK_NONBLOCK,
                     protocol);
    if (n < 0 || n >= (int)sizeof(path)) {
        return ERRNO(EINVAL);
    }

    // Wait for the the network stack to publish the socket device
    // if necessary.
    // TODO: move to a better mechanism when available.
    unsigned retry = 0;
    while ((r = __mxio_open(&io, path, 0, 0)) == ERR_NOT_FOUND) {
        if (retry >= 24) {
            // 10-second timeout
            return ERRNO(EIO);
        }
        retry++;
        mx_nanosleep(mx_deadline_after((retry < 8) ? MX_MSEC(250) : MX_MSEC(500)));
    }
    if (r < 0) {
        return ERROR(r);
    }
    if (type & SOCK_STREAM) {
        mxio_socket_set_stream_ops(io);
    } else if (type & SOCK_DGRAM) {
        mxio_socket_set_dgram_ops(io);
    }

    if (type & SOCK_NONBLOCK) {
        io->flags |= MXIO_FLAG_NONBLOCK;
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
    if (r == ERR_SHOULD_WAIT) {
        if (io->flags & MXIO_FLAG_NONBLOCK) {
            io->flags |= MXIO_FLAG_SOCKET_CONNECTING;
            mxio_release(io);
            return ERRNO(EINPROGRESS);
        }
        // going to wait for the completion
    } else {
        if (r == NO_ERROR) {
            io->flags |= MXIO_FLAG_SOCKET_CONNECTED;
        }
        mxio_release(io);
        return STATUS(r);
    }

    // wait for the completion
    uint32_t events = EPOLLOUT;
    mx_handle_t h;
    mx_signals_t sigs;
    io->ops->wait_begin(io, events, &h, &sigs);
    r = mx_object_wait_one(h, sigs, MX_TIME_INFINITE, &sigs);
    io->ops->wait_end(io, sigs, &events);
    if (!(events & EPOLLOUT)) {
        mxio_release(io);
        return ERRNO(EIO);
    }
    if (r < 0) {
        mxio_release(io);
        return ERROR(r);
    }

    // check the result
    int errno_;
    socklen_t errno_len = sizeof(errno_);
    r = mxio_getsockopt(io, SOL_SOCKET, SO_ERROR, &errno_, &errno_len);
    if (r < 0) {
        mxio_release(io);
        return ERRNO(EIO);
    }
    if (errno_ == 0) {
        io->flags |= MXIO_FLAG_SOCKET_CONNECTED;
    }
    mxio_release(io);
    if (errno_ != 0) {
        return ERRNO(errno_);
    }
    return 0;
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
    if (flags & ~SOCK_NONBLOCK) {
        return ERRNO(EINVAL);
    }

    mxio_t* io = fd_to_io(fd);
    if (io == NULL) {
        return ERRNO(EBADF);
    }

    mxio_t* io2;
    mx_status_t r;
    for (;;) {
        r = io->ops->open(io, MXRIO_SOCKET_DIR_ACCEPT, 0, 0, &io2);
        if (r == ERR_SHOULD_WAIT) {
            if (io->flags & MXIO_FLAG_NONBLOCK) {
                mxio_release(io);
                return EWOULDBLOCK;
            }
            // wait for an incoming connection
            uint32_t events = EPOLLIN;
            mx_handle_t h;
            mx_signals_t sigs;
            io->ops->wait_begin(io, events, &h, &sigs);
            r = mx_object_wait_one(h, sigs, MX_TIME_INFINITE, &sigs);
            io->ops->wait_end(io, sigs, &events);
            if (!(events & EPOLLIN)) {
                mxio_release(io);
                return ERRNO(EIO);
            }
            continue;
        } else if (r == NO_ERROR) {
            break;
        }
        mxio_release(io);
        return ERROR(r);
    }
    mxio_release(io);

    mxio_socket_set_stream_ops(io2);
    io2->flags |= MXIO_FLAG_SOCKET_CONNECTED;

    if (flags & SOCK_NONBLOCK) {
        io2->flags |= MXIO_FLAG_NONBLOCK;
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
        errno = EINVAL;
        return EAI_SYSTEM;
    }
    // Wait for the the network stack to publish the socket device
    // if necessary.
    // TODO: move to a better mechanism when available.
    unsigned retry = 0;
    while ((r = __mxio_open(&io, MXRIO_SOCKET_ROOT "/" MXRIO_SOCKET_DIR_NONE,
                            0, 0)) == ERR_NOT_FOUND) {
        if (retry >= 24) {
            // 10-second timeout
            return EAI_AGAIN;
        }
        retry++;
        mx_nanosleep(mx_deadline_after((retry < 8) ? MX_MSEC(250) : MX_MSEC(500)));
    }
    if (r < 0) {
        errno = mxio_status_to_errno(r);
        return EAI_SYSTEM;
    }

    static_assert(sizeof(mxrio_gai_req_reply_t) <= MXIO_CHUNK_SIZE,
                  "this type should be no larger than MXIO_CHUNK_SIZE");

    mxrio_gai_req_reply_t gai;

    gai.req.node_is_null = (node == NULL) ? 1 : 0;
    gai.req.service_is_null = (service == NULL) ? 1 : 0;
    gai.req.hints_is_null = (hints == NULL) ? 1 : 0;
    if (node) {
        strncpy(gai.req.node, node, MXRIO_GAI_REQ_NODE_MAXLEN);
        gai.req.node[MXRIO_GAI_REQ_NODE_MAXLEN-1] = '\0';
    }
    if (service) {
        strncpy(gai.req.service, service, MXRIO_GAI_REQ_SERVICE_MAXLEN);
        gai.req.service[MXRIO_GAI_REQ_SERVICE_MAXLEN-1] = '\0';
    }
    if (hints) {
        if (hints->ai_addrlen != 0 || hints->ai_addr != NULL ||
            hints->ai_canonname != NULL || hints->ai_next != NULL) {
            errno = EINVAL;
            return EAI_SYSTEM;
        }
        memcpy(&gai.req.hints, hints, sizeof(struct addrinfo));
    }

    r = io->ops->misc(io, MXRIO_GETADDRINFO, 0, sizeof(mxrio_gai_reply_t),
                      &gai, sizeof(gai));
    io->ops->close(io);
    mxio_release(io);

    if (r < 0) {
        errno = mxio_status_to_errno(r);
        return EAI_SYSTEM;
    }
    if (gai.reply.retval == 0) {
        // alloc the memory for the out param
        mxrio_gai_reply_t* reply = calloc(1, sizeof(*reply));
        // copy the reply
        memcpy(reply, &gai.reply, sizeof(*reply));

        // link all entries in the reply
        struct addrinfo *next = NULL;
        for (int i = reply->nres - 1; i >= 0; --i) {
            // adjust ai_addr to point the new address if not NULL
            if (reply->res[i].ai.ai_addr != NULL) {
                reply->res[i].ai.ai_addr =
                    (struct sockaddr*)&reply->res[i].addr;
            }
            reply->res[i].ai.ai_next = next;
            next = &reply->res[i].ai;
        }
        // the top of the reply must be the first addrinfo in the list
        assert(next == (struct addrinfo*)reply);
        *res = next;
    }
    return gai.reply.retval;
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

static mx_status_t mxio_getsockopt(mxio_t* io, int level, int optname,
                            void* restrict optval, socklen_t* restrict optlen) {
    if (optval == NULL || optlen == NULL) {
        return ERRNO(EINVAL);
    }

    mxrio_sockopt_req_reply_t req_reply;
    req_reply.level = level;
    req_reply.optname = optname;
    mx_status_t r = io->ops->misc(io, MXRIO_GETSOCKOPT, 0,
                                  sizeof(mxrio_sockopt_req_reply_t),
                                  &req_reply, sizeof(req_reply));
    if (r < 0) {
        return r;
    }
    socklen_t avail = *optlen;
    *optlen = req_reply.optlen;
    memcpy(optval, req_reply.optval,
           (avail < req_reply.optlen) ? avail : req_reply.optlen);

    return NO_ERROR;
}

int getsockopt(int fd, int level, int optname, void* restrict optval,
               socklen_t* restrict optlen) {
    mxio_t* io = fd_to_io(fd);
    if (io == NULL) {
        return ERRNO(EBADF);
    }

    mx_status_t r;
    r = mxio_getsockopt(io, level, optname, optval, optlen);
    mxio_release(io);

    return STATUS(r);
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
        mxio_release(io);
        return ERRNO(EINVAL);
    }
    memcpy(req.optval, optval, optlen);
    req.optlen = optlen;
    mx_status_t r = io->ops->misc(io, MXRIO_SETSOCKOPT, 0, 0, &req,
                                  sizeof(req));
    mxio_release(io);
    return STATUS(r);
}

static ssize_t mxio_sendmsg(mxio_t* io, const struct msghdr* msg, int flags) {
    mx_status_t r = io->ops->sendmsg(io, msg, flags);
    // TODO: better error codes?
    if (r == ERR_WRONG_TYPE)
        return ERRNO(ENOTSOCK);
    else if (r == ERR_BAD_STATE)
        return ERRNO(ENOTCONN);
    else if (r == ERR_ALREADY_EXISTS)
        return ERRNO(EISCONN);
    return STATUS(r);
}

static ssize_t mxio_sendto(mxio_t* io, const void* buf, size_t buflen, int flags, const struct sockaddr* addr, socklen_t addrlen) {
    struct iovec iov;
    iov.iov_base = (void*)buf;
    iov.iov_len = buflen;

    struct msghdr msg;
    msg.msg_name = (void*)addr;
    msg.msg_namelen = addrlen;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = NULL;
    msg.msg_controllen = 0;
    msg.msg_flags = 0; // this field is ignored

    return mxio_sendmsg(io, &msg, flags);
}

ssize_t mxio_send(mxio_t* io, const void* buf, size_t len, int flags) {
    return mxio_sendto(io, buf, len, flags, NULL, 0);
}

static ssize_t mxio_recvmsg(mxio_t* io, struct msghdr* msg, int flags) {
    mx_status_t r = io->ops->recvmsg(io, msg, flags);
    // TODO: better error codes?
    if (r == ERR_WRONG_TYPE)
        return ERRNO(ENOTSOCK);
    else if (r == ERR_BAD_STATE)
        return ERRNO(ENOTCONN);
    else if (r == ERR_ALREADY_EXISTS)
        return ERRNO(EISCONN);
    return STATUS(r);
}

static ssize_t mxio_recvfrom(mxio_t* io, void* restrict buf, size_t buflen, int flags, struct sockaddr* restrict addr, socklen_t* restrict addrlen) {
    struct iovec iov;
    iov.iov_base = buf;
    iov.iov_len = buflen;

    struct msghdr msg;
    msg.msg_name = addr;
    msg.msg_namelen = (addrlen == NULL) ? 0 : *addrlen;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = NULL;
    msg.msg_controllen = 0;
    msg.msg_flags = 0;

    ssize_t r = mxio_recvmsg(io, &msg, flags);
    if (addrlen != NULL)
        *addrlen = msg.msg_namelen;
    return r;
}

ssize_t mxio_recv(mxio_t* io, void* buf, size_t len, int flags) {
    return mxio_recvfrom(io, buf, len, flags, NULL, NULL);
}

ssize_t sendmsg(int fd, const struct msghdr *msg, int flags) {
    mxio_t* io = fd_to_io(fd);
    if (io == NULL) {
        return ERRNO(EBADF);
    }
    ssize_t r = mxio_sendmsg(io, msg, flags);
    mxio_release(io);
    return r;
}

ssize_t sendto(int fd, const void* buf, size_t buflen, int flags, const struct sockaddr* addr, socklen_t addrlen) {
    mxio_t* io = fd_to_io(fd);
    if (io == NULL) {
        return ERRNO(EBADF);
    }
    ssize_t r = mxio_sendto(io, buf, buflen, flags, addr, addrlen);
    mxio_release(io);
    return r;
}

ssize_t send(int fd, const void* buf, size_t len, int flags) {
    mxio_t* io = fd_to_io(fd);
    if (io == NULL) {
        return ERRNO(EBADF);
    }
    ssize_t r = mxio_send(io, buf, len, flags);
    mxio_release(io);
    return r;
}

ssize_t recvmsg(int fd, struct msghdr* msg, int flags) {
    mxio_t* io = fd_to_io(fd);
    if (io == NULL) {
        return ERRNO(EBADF);
    }
    ssize_t r = mxio_recvmsg(io, msg, flags);
    mxio_release(io);
    return r;
}

ssize_t recvfrom(int fd, void* restrict buf, size_t buflen, int flags, struct sockaddr* restrict addr, socklen_t* restrict addrlen) {
    mxio_t* io = fd_to_io(fd);
    if (io == NULL) {
        return ERRNO(EBADF);
    }
    ssize_t r = mxio_recvfrom(io, buf, buflen, flags, addr, addrlen);
    mxio_release(io);
    return r;
}

ssize_t recv(int fd, void* buf, size_t len, int flags) {
    mxio_t* io = fd_to_io(fd);
    if (io == NULL) {
        return ERRNO(EBADF);
    }
    ssize_t r = mxio_recv(io, buf, len, flags);
    mxio_release(io);
    return r;
}

int shutdown(int fd, int how) {
    mxio_t* io;
    if ((io = fd_to_io(fd)) == NULL) {
        return ERRNO(EBADF);
    }
    if (!(io->flags & MXIO_FLAG_SOCKET)) {
        mxio_release(io);
        return ERRNO(ENOTSOCK);
    }
    if (!(io->flags & MXIO_FLAG_SOCKET_CONNECTED)) {
        mxio_release(io);
        return ERRNO(ENOTCONN);
    }
    mx_status_t r = mxio_socket_shutdown(io, how);
    mxio_release(io);
    return STATUS(r);
}
