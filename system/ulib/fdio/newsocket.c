// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <poll.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>

#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include <fdio/io.h>
#include <fdio/remoteio.h>
#include <fdio/socket.h>
#include <fdio/util.h>

#include "private-socket.h"

static bool is_rio_message_valid(zxsio_msg_t* msg) {
    if ((msg->datalen > ZXSIO_PAYLOAD_SZ) ||
        (msg->hcount > 0)) {
        return false;
    }
    return true;
}

static bool is_rio_message_reply_valid(zxsio_msg_t* msg, uint32_t size) {
    if ((size < ZXSIO_HDR_SZ) ||
        (msg->datalen != (size - ZXSIO_HDR_SZ))) {
        return false;
    }
    return is_rio_message_valid(msg);
}

zx_status_t zxsio_accept(fdio_t* io, zx_handle_t* s2) {
    zxsio_t* sio = (zxsio_t*)io;

    if (!(sio->flags & ZXSIO_DID_LISTEN)) {
        return ZX_ERR_BAD_STATE;
    }

    zx_status_t r;
    for (;;) {
        r = zx_socket_accept(sio->s, s2);
        if (r == ZX_ERR_SHOULD_WAIT) {
            if (io->ioflag & IOFLAG_NONBLOCK) {
                return ZX_ERR_SHOULD_WAIT;
            }

            // wait for an incoming connection
            zx_signals_t pending;
            r = zx_object_wait_one(sio->s, ZX_SOCKET_ACCEPT | ZX_SOCKET_PEER_CLOSED, ZX_TIME_INFINITE, &pending);
            if (r < 0) {
                return r;
            }
            if (pending & ZX_SOCKET_ACCEPT) {
                continue;
            }
            if (pending & ZX_SOCKET_PEER_CLOSED) {
                return ZX_ERR_PEER_CLOSED;
            }
            // impossible
            return ZX_ERR_INTERNAL;
        }
        break;
    }
    return r;
}

static ssize_t zxsio_read_stream(fdio_t* io, void* data, size_t len) {
    zxsio_t* sio = (zxsio_t*)io;
    int nonblock = sio->io.ioflag & IOFLAG_NONBLOCK;

    // TODO: let the generic read() to do this loop
    for (;;) {
        ssize_t r;
        size_t bytes_read;
        if ((r = zx_socket_read(sio->s, 0, data, len, &bytes_read)) == ZX_OK) {
            // zx_socket_read() sets *actual to the number of bytes in the buffer when data is NULL
            // and len is 0. read() should return 0 in that case.
            if (len == 0) {
                return 0;
            } else {
                return (ssize_t)bytes_read;
            }
        }
        if (r == ZX_ERR_PEER_CLOSED || r == ZX_ERR_BAD_STATE) {
            return 0;
        } else if (r == ZX_ERR_SHOULD_WAIT && !nonblock) {
            zx_signals_t pending;
            r = zx_object_wait_one(sio->s,
                                   ZX_SOCKET_READABLE | ZX_SOCKET_PEER_CLOSED | ZX_SOCKET_READ_DISABLED,
                                   ZX_TIME_INFINITE, &pending);
            if (r < 0) {
                return r;
            }
            if (pending & ZX_SOCKET_READABLE) {
                continue;
            }
            if (pending & (ZX_SOCKET_PEER_CLOSED | ZX_SOCKET_READ_DISABLED)) {
                return 0;
            }
            // impossible
            return ZX_ERR_INTERNAL;
        }
        return r;
    }
}

static ssize_t zxsio_recvfrom(fdio_t* io, void* data, size_t len, int flags, struct sockaddr* restrict addr, socklen_t* restrict addrlen) {
    struct iovec iov;
    iov.iov_base = data;
    iov.iov_len = len;

    struct msghdr msg;
    msg.msg_name = addr;
    // the caller (recvfrom) checks if addrlen is NULL.
    msg.msg_namelen = (addr == NULL) ? 0 : *addrlen;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = NULL;
    msg.msg_controllen = 0;
    msg.msg_flags = 0;

    ssize_t r = io->ops->recvmsg(io, &msg, flags);
    if (addr != NULL)
        *addrlen = msg.msg_namelen;

    return r;
}

static ssize_t zxsio_write_stream(fdio_t* io, const void* data, size_t len) {
    zxsio_t* sio = (zxsio_t*)io;
    int nonblock = sio->io.ioflag & IOFLAG_NONBLOCK;

    // TODO: let the generic write() to do this loop
    for (;;) {
        ssize_t r;
        if ((r = zx_socket_write(sio->s, 0, data, len, &len)) == ZX_OK) {
            return (ssize_t) len;
        }
        if (r == ZX_ERR_SHOULD_WAIT && !nonblock) {
            zx_signals_t pending;
            r = zx_object_wait_one(sio->s,
                                   ZX_SOCKET_WRITABLE | ZX_SOCKET_WRITE_DISABLED | ZX_SOCKET_PEER_CLOSED,
                                   ZX_TIME_INFINITE, &pending);
            if (r < 0) {
                return r;
            }
            if (pending & (ZX_SOCKET_WRITE_DISABLED | ZX_SOCKET_PEER_CLOSED)) {
                return ZX_ERR_PEER_CLOSED;
            }
            if (pending & ZX_SOCKET_WRITABLE) {
                continue;
            }
            // impossible
            return ZX_ERR_INTERNAL;
        }
        return r;
    }
}

static ssize_t zxsio_sendto(fdio_t* io, const void* data, size_t len, int flags, const struct sockaddr* addr, socklen_t addrlen) {
    struct iovec iov;
    iov.iov_base = (void*)data;
    iov.iov_len = len;

    struct msghdr msg;
    msg.msg_name = (void*)addr;
    msg.msg_namelen = addrlen;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = NULL;
    msg.msg_controllen = 0;
    msg.msg_flags = 0; // this field is ignored

    return io->ops->sendmsg(io, &msg, flags);
}


static ssize_t zxsio_recvmsg_stream(fdio_t* io, struct msghdr* msg, int flags) {
    if (flags != 0) {
        // TODO: support MSG_OOB
        return ZX_ERR_NOT_SUPPORTED;
    }
    if (!(io->ioflag & IOFLAG_SOCKET_CONNECTED)) {
        return ZX_ERR_BAD_STATE;
    }
    // we ignore msg_name and msg_namelen members.
    // (this is a consistent behavior with other OS implementations for TCP protocol)
    ssize_t total = 0;
    for (int i = 0; i < msg->msg_iovlen; i++) {
        struct iovec *iov = &msg->msg_iov[i];
        ssize_t n = zxsio_read_stream(io, iov->iov_base, iov->iov_len);
        if (n < 0) {
            return n;
        }
        total += n;
        if ((size_t)n != iov->iov_len) {
            break;
        }
    }
    return total;
}

static ssize_t zxsio_sendmsg_stream(fdio_t* io, const struct msghdr* msg, int flags) {
    if (flags != 0) {
        // TODO: support MSG_OOB
        return ZX_ERR_NOT_SUPPORTED;
    }
    // TODO: support flags and control messages
    if (io->ioflag & IOFLAG_SOCKET_CONNECTED) {
        // if connected, can't specify address
        if (msg->msg_name != NULL || msg->msg_namelen != 0) {
            return ZX_ERR_ALREADY_EXISTS;
        }
    } else {
        return ZX_ERR_BAD_STATE;
    }
    ssize_t total = 0;
    for (int i = 0; i < msg->msg_iovlen; i++) {
        struct iovec *iov = &msg->msg_iov[i];
        if (iov->iov_len <= 0) {
            return ZX_ERR_INVALID_ARGS;
        }
        ssize_t n = zxsio_write_stream(io, iov->iov_base, iov->iov_len);
        if (n < 0) {
            return n;
        }
        total += n;
        if ((size_t)n != iov->iov_len) {
            break;
        }
    }
    return total;
}

static zx_status_t zxsio_clone_stream(fdio_t* io, zx_handle_t* handles, uint32_t* types) {
    // TODO: support unconnected sockets
    if (!(io->ioflag & IOFLAG_SOCKET_CONNECTED)) {
        return ZX_ERR_BAD_STATE;
    }
    zxsio_t* sio = (void*)io;
    zx_status_t r = zx_handle_duplicate(sio->s, ZX_RIGHT_SAME_RIGHTS, handles);
    if (r < 0) {
        return r;
    }
    types[0] = PA_FDIO_SOCKET;
    return 1;
}

static zx_status_t zxsio_unwrap_stream(fdio_t* io, zx_handle_t* handles, uint32_t* types) {
    // TODO: support unconnected sockets
    if (!(io->ioflag & IOFLAG_SOCKET_CONNECTED)) {
        return ZX_ERR_BAD_STATE;
    }
    zxsio_t* sio = (void*)io;
    zx_status_t r;
    handles[0] = sio->s;
    types[0] = PA_FDIO_SOCKET;
    r = 1;
    return r;
}

static void zxsio_wait_begin_stream(fdio_t* io, uint32_t events, zx_handle_t* handle, zx_signals_t* _signals) {
    zxsio_t* sio = (void*)io;
    *handle = sio->s;
    // TODO: locking for flags/state
    if (io->ioflag & IOFLAG_SOCKET_CONNECTING) {
        // check the connection state
        zx_signals_t observed;
        zx_status_t r;
        r = zx_object_wait_one(sio->s, ZXSIO_SIGNAL_CONNECTED, 0u,
                               &observed);
        if (r == ZX_OK || r == ZX_ERR_TIMED_OUT) {
            if (observed & ZXSIO_SIGNAL_CONNECTED) {
                io->ioflag &= ~IOFLAG_SOCKET_CONNECTING;
                io->ioflag |= IOFLAG_SOCKET_CONNECTED;
            }
        }
    }
    zx_signals_t signals = ZXSIO_SIGNAL_ERROR;
    if (io->ioflag & IOFLAG_SOCKET_CONNECTED) {
        // if socket is connected
        if (events & POLLIN) {
            signals |= ZX_SOCKET_READABLE | ZX_SOCKET_READ_DISABLED | ZX_SOCKET_PEER_CLOSED;
        }
        if (events & POLLOUT) {
            signals |= ZX_SOCKET_WRITABLE | ZX_SOCKET_WRITE_DISABLED;
        }
    } else {
        // if socket is not connected
        if (events & POLLIN) {
            // signal when a listening socket gets an incoming connection
            // or a connecting socket gets connected and receives data
            signals |= ZX_SOCKET_ACCEPT |
                ZX_SOCKET_READABLE | ZX_SOCKET_READ_DISABLED | ZX_SOCKET_PEER_CLOSED;
        }
        if (events & POLLOUT) {
            // signal when connect() operation is finished
            signals |= ZXSIO_SIGNAL_OUTGOING;
        }
    }
    if (events & POLLRDHUP) {
        signals |= ZX_SOCKET_READ_DISABLED | ZX_SOCKET_PEER_CLOSED;
    }
    *_signals = signals;
}

static void zxsio_wait_end_stream(fdio_t* io, zx_signals_t signals, uint32_t* _events) {
    // check the connection state
    if (io->ioflag & IOFLAG_SOCKET_CONNECTING) {
        if (signals & ZXSIO_SIGNAL_CONNECTED) {
            io->ioflag &= ~IOFLAG_SOCKET_CONNECTING;
            io->ioflag |= IOFLAG_SOCKET_CONNECTED;
        }
    }
    uint32_t events = 0;
    if (io->ioflag & IOFLAG_SOCKET_CONNECTED) {
        if (signals & (ZX_SOCKET_READABLE | ZX_SOCKET_READ_DISABLED | ZX_SOCKET_PEER_CLOSED)) {
            events |= POLLIN;
        }
        if (signals & (ZX_SOCKET_WRITABLE | ZX_SOCKET_WRITE_DISABLED)) {
            events |= POLLOUT;
        }
    } else {
        if (signals & (ZX_SOCKET_ACCEPT | ZX_SOCKET_PEER_CLOSED)) {
            events |= POLLIN;
        }
        if (signals & ZXSIO_SIGNAL_OUTGOING) {
            events |= POLLOUT;
        }
    }
    if (signals & ZXSIO_SIGNAL_ERROR) {
        events |= POLLERR;
    }
    if (signals & (ZX_SOCKET_READ_DISABLED | ZX_SOCKET_PEER_CLOSED)) {
        events |= POLLRDHUP;
    }
    *_events = events;
}

static ssize_t zxsio_posix_ioctl_stream(fdio_t* io, int req, va_list va) {
    zxsio_t* sio = (zxsio_t*)io;
    switch (req) {
    case FIONREAD: {
        zx_status_t r;
        size_t avail;
        if ((r = zx_socket_read(sio->s, 0, NULL, 0, &avail)) < 0) {
            return r;
        }
        if (avail > INT_MAX) {
            avail = INT_MAX;
        }
        int* actual = va_arg(va, int*);
        *actual = avail;
        return ZX_OK;
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

static ssize_t zxsio_rx_dgram(fdio_t* io, void* buf, size_t buflen) {
    return zxsio_read_stream(io, buf, buflen);
}

static ssize_t zxsio_tx_dgram(fdio_t* io, const void* buf, size_t buflen) {
    zx_status_t r = zxsio_write_stream(io, buf, buflen);
    return (r < 0) ? r : ZX_OK;
}

static ssize_t zxsio_recvmsg_dgram(fdio_t* io, struct msghdr* msg, int flags);
static ssize_t zxsio_sendmsg_dgram(fdio_t* io, const struct msghdr* msg, int flags);

static ssize_t zxsio_read_dgram(fdio_t* io, void* data, size_t len) {
    struct iovec iov;
    iov.iov_base = data;
    iov.iov_len = len;

    struct msghdr msg;
    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = NULL;
    msg.msg_controllen = 0;
    msg.msg_flags = 0;

    return zxsio_recvmsg_dgram(io, &msg, 0);
}

static ssize_t zxsio_write_dgram(fdio_t* io, const void* data, size_t len) {
    struct iovec iov;
    iov.iov_base = (void*)data;
    iov.iov_len = len;

    struct msghdr msg;
    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = NULL;
    msg.msg_controllen = 0;
    msg.msg_flags = 0;

    return zxsio_sendmsg_dgram(io, &msg, 0);
}

static ssize_t zxsio_recvmsg_dgram(fdio_t* io, struct msghdr* msg, int flags) {
    if (flags != 0) {
        // TODO: support MSG_OOB
        return ZX_ERR_NOT_SUPPORTED;
    }
    // Read 1 extra byte to detect if the buffer is too small to fit the whole
    // packet, so we can set MSG_TRUNC flag if necessary.
    size_t mlen = FDIO_SOCKET_MSG_HEADER_SIZE + 1;
    for (int i = 0; i < msg->msg_iovlen; i++) {
        struct iovec *iov = &msg->msg_iov[i];
        if (iov->iov_len <= 0) {
            return ZX_ERR_INVALID_ARGS;
        }
        mlen += iov->iov_len;
    }

    // TODO: avoid malloc
    fdio_socket_msg_t* m = malloc(mlen);
    ssize_t n = zxsio_rx_dgram(io, m, mlen);
    if (n < 0) {
        free(m);
        return n;
    }
    if ((size_t)n < FDIO_SOCKET_MSG_HEADER_SIZE) {
        free(m);
        return ZX_ERR_INTERNAL;
    }
    n -= FDIO_SOCKET_MSG_HEADER_SIZE;
    if (msg->msg_name != NULL) {
        int bytes_to_copy = (msg->msg_namelen < m->addrlen) ? msg->msg_namelen : m->addrlen;
        memcpy(msg->msg_name, &m->addr, bytes_to_copy);
    }
    msg->msg_namelen = m->addrlen;
    msg->msg_flags = m->flags;
    char* data = m->data;
    size_t resid = n;
    for (int i = 0; i < msg->msg_iovlen; i++) {
        struct iovec *iov = &msg->msg_iov[i];
        if (resid == 0) {
            iov->iov_len = 0;
        } else {
            if (resid < iov->iov_len)
                iov->iov_len = resid;
            memcpy(iov->iov_base, data, iov->iov_len);
            data += iov->iov_len;
            resid -= iov->iov_len;
        }
    }

    if (resid > 0) {
        msg->msg_flags |= MSG_TRUNC;
        n -= resid;
    }

    free(m);
    return n;
}

static ssize_t zxsio_sendmsg_dgram(fdio_t* io, const struct msghdr* msg, int flags) {
    if (flags != 0) {
        // TODO: MSG_OOB
        return ZX_ERR_NOT_SUPPORTED;
    }
    // TODO: support flags and control messages
    if (io->ioflag & IOFLAG_SOCKET_CONNECTED) {
        // if connected, can't specify address
        if (msg->msg_name != NULL || msg->msg_namelen != 0) {
            return ZX_ERR_ALREADY_EXISTS;
        }
    }
    ssize_t n = 0;
    for (int i = 0; i < msg->msg_iovlen; i++) {
        struct iovec *iov = &msg->msg_iov[i];
        if (iov->iov_len <= 0) {
            return ZX_ERR_INVALID_ARGS;
        }
        n += iov->iov_len;
    }
    size_t mlen = n + FDIO_SOCKET_MSG_HEADER_SIZE;

    // TODO: avoid malloc m
    fdio_socket_msg_t* m = malloc(mlen);
    if (msg->msg_name != NULL) {
        memcpy(&m->addr, msg->msg_name, msg->msg_namelen);
    }
    m->addrlen = msg->msg_namelen;
    m->flags = flags;
    char* data = m->data;
    for (int i = 0; i < msg->msg_iovlen; i++) {
        struct iovec *iov = &msg->msg_iov[i];
        memcpy(data, iov->iov_base, iov->iov_len);
        data += iov->iov_len;
    }
    ssize_t r = zxsio_tx_dgram(io, m, mlen);
    free(m);
    return r == ZX_OK ? n : r;
}

static void zxsio_wait_begin_dgram(fdio_t* io, uint32_t events, zx_handle_t* handle, zx_signals_t* _signals) {
    zxsio_t* sio = (void*)io;
    *handle = sio->s;
    zx_signals_t signals = ZXSIO_SIGNAL_ERROR;
    if (events & POLLIN) {
        signals |= ZX_SOCKET_READABLE | ZX_SOCKET_READ_DISABLED | ZX_SOCKET_PEER_CLOSED;
    }
    if (events & POLLOUT) {
        signals |= ZX_SOCKET_WRITABLE | ZX_SOCKET_WRITE_DISABLED;
    }
    if (events & POLLRDHUP) {
        signals |=  ZX_SOCKET_READ_DISABLED | ZX_SOCKET_PEER_CLOSED;
    }
    *_signals = signals;
}

static void zxsio_wait_end_dgram(fdio_t* io, zx_signals_t signals, uint32_t* _events) {
    uint32_t events = 0;
    if (signals & (ZX_SOCKET_READABLE | ZX_SOCKET_READ_DISABLED | ZX_SOCKET_PEER_CLOSED)) {
        events |= POLLIN;
    }
    if (signals & (ZX_SOCKET_WRITABLE | ZX_SOCKET_WRITE_DISABLED)) {
        events |= POLLOUT;
    }
    if (signals & ZXSIO_SIGNAL_ERROR) {
        events |= POLLERR;
    }
    if (signals & (ZX_SOCKET_READ_DISABLED | ZX_SOCKET_PEER_CLOSED)) {
        events |= POLLRDHUP;
    }
    *_events = events;
}

static zx_status_t zxsio_write_control(zxsio_t* sio, zxsio_msg_t* msg) {
    for (;;) {
        ssize_t r;
        size_t len = ZXSIO_HDR_SZ + msg->datalen;
        if ((r = zx_socket_write(sio->s, ZX_SOCKET_CONTROL, msg, len, &len)) == ZX_OK) {
            return (ssize_t) len;
        }
        if (r == ZX_ERR_SHOULD_WAIT) {
            zx_signals_t pending;
            r = zx_object_wait_one(sio->s,
                                   ZX_SOCKET_CONTROL_WRITABLE | ZX_SOCKET_PEER_CLOSED,
                                   ZX_TIME_INFINITE, &pending);
            if (r < 0) {
                return r;
            }
            if (pending & ZX_SOCKET_PEER_CLOSED) {
                return ZX_ERR_PEER_CLOSED;
            }
            if (pending & ZX_SOCKET_CONTROL_WRITABLE) {
                continue;
            }
            // impossible
            return ZX_ERR_INTERNAL;
        }
        return r;
    }
}

static ssize_t zxsio_read_control(zxsio_t* sio, void* data, size_t len) {
    // TODO: let the generic read() to do this loop
    for (;;) {
        ssize_t r;
        size_t bytes_read;
        if ((r = zx_socket_read(sio->s, ZX_SOCKET_CONTROL, data, len, &bytes_read)) == ZX_OK) {
            // zx_socket_read() sets *actual to the number of bytes in the buffer when data is NULL
            // and len is 0. read() should return 0 in that case.
            if (len == 0) {
                return 0;
            } else {
                return (ssize_t)bytes_read;
            }
        }
        if (r == ZX_ERR_PEER_CLOSED || r == ZX_ERR_BAD_STATE) {
            return 0;
        } else if (r == ZX_ERR_SHOULD_WAIT) {
            zx_signals_t pending;
            r = zx_object_wait_one(sio->s,
                                   ZX_SOCKET_CONTROL_READABLE | ZX_SOCKET_PEER_CLOSED,
                                   ZX_TIME_INFINITE, &pending);
            if (r < 0) {
                return r;
            }
            if (pending & ZX_SOCKET_CONTROL_READABLE) {
                continue;
            }
            if (pending & ZX_SOCKET_PEER_CLOSED) {
                return 0;
            }
            // impossible
            return ZX_ERR_INTERNAL;
        }
        return r;
    }
}

static zx_status_t zxsio_txn(zxsio_t* sio, zxsio_msg_t* msg) {
    if (!is_rio_message_valid(msg)) {
        return ZX_ERR_INVALID_ARGS;
    }

    zx_status_t r = zxsio_write_control(sio, msg);
    if (r < 0)
        return r;
    r = zxsio_read_control(sio, msg, sizeof(*msg));
    if (r < 0)
        return r;

    size_t dsize = (size_t)r;
    // check for protocol errors
    if (!is_rio_message_reply_valid(msg, dsize) ||
        (ZXRIO_OP(msg->op) != ZXRIO_STATUS)) {
        return ZX_ERR_IO;
    }
    return msg->arg;
}

static zx_status_t zxsio_misc(fdio_t* io, uint32_t op, int64_t off,
                              uint32_t maxreply, void* ptr, size_t len) {
    zxsio_t* sio = (zxsio_t*)io;
    zxsio_msg_t msg;
    zx_status_t r;

    if ((len > ZXSIO_PAYLOAD_SZ) || (maxreply > ZXSIO_PAYLOAD_SZ)) {
        return ZX_ERR_INVALID_ARGS;
    }

    switch (op) {
    case ZXRIO_GETADDRINFO:
    case ZXRIO_GETSOCKNAME:
    case ZXRIO_GETPEERNAME:
    case ZXRIO_GETSOCKOPT:
    case ZXRIO_SETSOCKOPT:
    case ZXRIO_CONNECT:
    case ZXRIO_BIND:
    case ZXRIO_LISTEN:
    case ZXRIO_FCNTL:
        break;
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }

    memset(&msg, 0, ZXSIO_HDR_SZ);
    msg.op = op;
    msg.arg = maxreply;
    msg.arg2.off = off;
    msg.datalen = len;
    if (ptr && len > 0) {
        memcpy(msg.data, ptr, len);
    }

    if ((r = zxsio_txn(sio, &msg)) < 0) {
        return r;
    }
    if (msg.datalen > maxreply) {
        return ZX_ERR_IO;
    }
    if (ptr && msg.datalen > 0) {
        memcpy(ptr, msg.data, msg.datalen);
    }

    if (op == ZXRIO_LISTEN && r == ZX_OK) {
        sio->flags |= ZXSIO_DID_LISTEN;
    }

    return r;
}

static zx_status_t zxsio_close(fdio_t* io) {
    zxsio_t* sio = (zxsio_t*)io;
    zxsio_msg_t msg;
    zx_status_t r;

    memset(&msg, 0, ZXSIO_HDR_SZ);
    msg.op = ZXRIO_CLOSE;
    r = zxsio_txn(sio, &msg);

    zx_handle_t h = sio->s;
    sio->s = 0;
    zx_handle_close(h);

    return r;
}

static ssize_t zxsio_ioctl(fdio_t* io, uint32_t op, const void* in_buf,
                           size_t in_len, void* out_buf, size_t out_len) {
    zxsio_t* sio = (zxsio_t*)io;
    const uint8_t* data = in_buf;
    zx_status_t r = 0;
    zxsio_msg_t msg;

    if (in_len > ZXSIO_PAYLOAD_SZ || out_len > ZXSIO_PAYLOAD_SZ) {
        return ZX_ERR_INVALID_ARGS;
    }

    if (IOCTL_KIND(op) != IOCTL_KIND_DEFAULT) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    memset(&msg, 0, ZXSIO_HDR_SZ);
    msg.op = ZXRIO_IOCTL;
    msg.datalen = in_len;
    msg.arg = out_len;
    msg.arg2.op = op;
    memcpy(msg.data, data, in_len);

    if ((r = zxsio_txn(sio, &msg)) < 0) {
        return r;
    }

    size_t copy_len = msg.datalen;
    if (msg.datalen > out_len) {
        copy_len = out_len;
    }

    memcpy(out_buf, msg.data, copy_len);
    return r;
}

static fdio_ops_t fdio_socket_stream_ops = {
    .read = zxsio_read_stream,
    .read_at = fdio_default_read_at,
    .write = zxsio_write_stream,
    .write_at = fdio_default_write_at,
    .recvfrom = zxsio_recvfrom,
    .sendto = zxsio_sendto,
    .recvmsg = zxsio_recvmsg_stream,
    .sendmsg = zxsio_sendmsg_stream,
    .seek = fdio_default_seek,
    .misc = zxsio_misc,
    .close = zxsio_close,
    .open = fdio_default_open,
    .clone = zxsio_clone_stream,
    .ioctl = zxsio_ioctl,
    .wait_begin = zxsio_wait_begin_stream,
    .wait_end = zxsio_wait_end_stream,
    .unwrap = zxsio_unwrap_stream,
    .shutdown = fdio_socket_shutdown,
    .posix_ioctl = zxsio_posix_ioctl_stream,
    .get_vmo = fdio_default_get_vmo,
};

static fdio_ops_t fdio_socket_dgram_ops = {
    .read = zxsio_read_dgram,
    .read_at = fdio_default_read_at,
    .write = zxsio_write_dgram,
    .write_at = fdio_default_write_at,
    .recvfrom = zxsio_recvfrom,
    .sendto = zxsio_sendto,
    .recvmsg = zxsio_recvmsg_dgram,
    .sendmsg = zxsio_sendmsg_dgram,
    .seek = fdio_default_seek,
    .misc = zxsio_misc,
    .close = zxsio_close,
    .open = fdio_default_open,
    .clone = fdio_default_clone,
    .ioctl = zxsio_ioctl,
    .wait_begin = zxsio_wait_begin_dgram,
    .wait_end = zxsio_wait_end_dgram,
    .unwrap = fdio_default_unwrap,
    .shutdown = fdio_socket_shutdown,
    .posix_ioctl = fdio_default_posix_ioctl, // not supported
    .get_vmo = fdio_default_get_vmo,
};

fdio_t* fdio_socket_create(zx_handle_t s, int flags) {
    zxsio_t* sio = calloc(1, sizeof(*sio));
    if (sio == NULL) {
        zx_handle_close(s);
        return NULL;
    }
    sio->io.ops = &fdio_socket_stream_ops; // default is stream
    sio->io.magic = FDIO_MAGIC;
    sio->io.refcount = 1;
    sio->io.ioflag = IOFLAG_SOCKET | flags;
    sio->s = s;
    sio->flags = 0;
    return &sio->io;
}

void fdio_socket_set_stream_ops(fdio_t* io) {
    zxsio_t* sio = (zxsio_t*)io;
    sio->io.ops = &fdio_socket_stream_ops;
}

void fdio_socket_set_dgram_ops(fdio_t* io) {
    zxsio_t* sio = (zxsio_t*)io;
    sio->io.ops = &fdio_socket_dgram_ops;
}

zx_status_t fdio_socket_shutdown(fdio_t* io, int how) {
    if (!(io->ioflag & IOFLAG_SOCKET_CONNECTED)) {
        return ZX_ERR_BAD_STATE;
    }
    zxsio_t* sio = (zxsio_t*)io;
    if (how == SHUT_WR || how == SHUT_RDWR) {
        // netstack expects this user signal to be set - raise it to keep that code working until
        // it learns about the read/write disabled signals.
        zx_object_signal_peer(sio->s, 0u, ZXSIO_SIGNAL_HALFCLOSED);
    }
    uint32_t options = 0;
    switch (how) {
    case SHUT_RD:
        options = ZX_SOCKET_SHUTDOWN_READ;
        break;
    case SHUT_WR:
        options = ZX_SOCKET_SHUTDOWN_WRITE;
        break;
    case SHUT_RDWR:
        options = ZX_SOCKET_SHUTDOWN_READ | ZX_SOCKET_SHUTDOWN_WRITE;
        break;
    }
    return zx_socket_write(sio->s, options, NULL, 0, NULL);
}
