// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include <magenta/processargs.h>
#include <magenta/syscalls.h>

#include <mxio/io.h>
#include <mxio/remoteio.h>
#include <mxio/socket.h>
#include <mxio/util.h>

#include "private-remoteio.h"


static ssize_t mxsio_read_stream(mxio_t* io, void* data, size_t len) {
    mxrio_t* rio = (mxrio_t*)io;
    int nonblock = rio->io.flags & MXIO_FLAG_NONBLOCK;

    // TODO: let the generic read() to do this loop
    for (;;) {
        ssize_t r;
        if ((r = mx_socket_read(rio->h2, 0, data, len, &len)) == NO_ERROR) {
            return (ssize_t) len;
        }
        if (r == ERR_PEER_CLOSED) {
            return 0;
        } else if (r == ERR_SHOULD_WAIT && !nonblock) {
            mx_signals_t pending;
            r = mx_object_wait_one(rio->h2,
                                   MX_SOCKET_READABLE | MX_SOCKET_PEER_CLOSED,
                                   MX_TIME_INFINITE, &pending);
            if (r < 0) {
                return r;
            }
            if (pending & MX_SOCKET_READABLE) {
                continue;
            }
            if (pending & MX_SOCKET_PEER_CLOSED) {
                return 0;
            }
            // impossible
            return ERR_INTERNAL;
        }
        return r;
    }
}

static ssize_t mxsio_write_stream(mxio_t* io, const void* data, size_t len) {
    mxrio_t* rio = (mxrio_t*)io;
    int nonblock = rio->io.flags & MXIO_FLAG_NONBLOCK;

    // TODO: let the generic write() to do this loop
    for (;;) {
        ssize_t r;
        if ((r = mx_socket_write(rio->h2, 0, data, len, &len)) == NO_ERROR) {
            return (ssize_t) len;
        }
        if (r == ERR_SHOULD_WAIT && !nonblock) {
            // No wait for PEER_CLOSED signal. PEER_CLOSED could be signaled
            // even if the socket is only half-closed for read.
            // TODO: how to detect if the write direction is closed?
            mx_signals_t pending;
            r = mx_object_wait_one(rio->h2,
                                   MX_SOCKET_WRITABLE,
                                   MX_TIME_INFINITE, &pending);
            if (r < 0) {
                return r;
            }
            if (pending & MX_SOCKET_WRITABLE) {
                continue;
            }
            // impossible
            return ERR_INTERNAL;
        }
        return r;
    }
}

static ssize_t mxsio_recvmsg_stream(mxio_t* io, struct msghdr* msg, int flags) {
    if (flags != 0) {
        // TODO: support MSG_OOB
        return ERR_NOT_SUPPORTED;
    }
    // TODO: support flags and control messages
    if (io->flags & MXIO_FLAG_SOCKET_CONNECTED) {
        // if connected, can't specify address
        if (msg->msg_name != NULL || msg->msg_namelen != 0) {
            return ERR_ALREADY_EXISTS;
        }
    } else {
        return ERR_BAD_STATE;
    }
    ssize_t total = 0;
    for (int i = 0; i < msg->msg_iovlen; i++) {
        struct iovec *iov = &msg->msg_iov[i];
        ssize_t n = mxsio_read_stream(io, iov->iov_base, iov->iov_len);
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

static ssize_t mxsio_sendmsg_stream(mxio_t* io, const struct msghdr* msg, int flags) {
    if (flags != 0) {
        // TODO: support MSG_OOB
        return ERR_NOT_SUPPORTED;
    }
    // TODO: support flags and control messages
    if (io->flags & MXIO_FLAG_SOCKET_CONNECTED) {
        // if connected, can't specify address
        if (msg->msg_name != NULL || msg->msg_namelen != 0) {
            return ERR_ALREADY_EXISTS;
        }
    } else {
        return ERR_BAD_STATE;
    }
    ssize_t total = 0;
    for (int i = 0; i < msg->msg_iovlen; i++) {
        struct iovec *iov = &msg->msg_iov[i];
        if (iov->iov_len <= 0) {
            return ERR_INVALID_ARGS;
        }
        ssize_t n = mxsio_write_stream(io, iov->iov_base, iov->iov_len);
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

static mx_status_t mxsio_clone_stream(mxio_t* io, mx_handle_t* handles, uint32_t* types) {
    // TODO: support unconnected sockets
    if (!(io->flags & MXIO_FLAG_SOCKET_CONNECTED)) {
        return ERR_BAD_STATE;
    }
    mxrio_t* rio = (void*)io;
    mxrio_object_t info;
    mx_status_t r = mxrio_getobject(rio->h, MXRIO_CLONE, "", 0, 0, &info);
    if (r < 0) {
        return r;
    }
    for (unsigned i = 0; i < info.hcount; i++) {
        types[i] = PA_MXIO_SOCKET;
    }
    memcpy(handles, info.handle, info.hcount * sizeof(mx_handle_t));
    return info.hcount;
}

static mx_status_t mxsio_unwrap_stream(mxio_t* io, mx_handle_t* handles, uint32_t* types) {
    // TODO: support unconnected sockets
    if (!(io->flags & MXIO_FLAG_SOCKET_CONNECTED)) {
        return ERR_BAD_STATE;
    }
    mxrio_t* rio = (void*)io;
    mx_status_t r;
    handles[0] = rio->h;
    types[0] = PA_MXIO_SOCKET;
    if (rio->h2 != 0) {
        handles[1] = rio->h2;
        types[1] = PA_MXIO_SOCKET;
        r = 2;
    } else {
        r = 1;
    }
    free(io);
    return r;
}

static void mxsio_wait_begin_stream(mxio_t* io, uint32_t events, mx_handle_t* handle, mx_signals_t* _signals) {
    mxrio_t* rio = (void*)io;
    *handle = rio->h2;
    // TODO: locking for flags/state
    if (io->flags & MXIO_FLAG_SOCKET_CONNECTING) {
        // check the connection state
        mx_signals_t observed;
        mx_status_t r;
        r = mx_object_wait_one(rio->h2, MXSIO_SIGNAL_CONNECTED, 0u,
                               &observed);
        if (r == NO_ERROR || r == ERR_TIMED_OUT) {
            if (observed & MXSIO_SIGNAL_CONNECTED) {
                io->flags &= ~MXIO_FLAG_SOCKET_CONNECTING;
                io->flags |= MXIO_FLAG_SOCKET_CONNECTED;
            }
        }
    }
    mx_signals_t signals = MXSIO_SIGNAL_ERROR;
    if (io->flags & MXIO_FLAG_SOCKET_CONNECTED) {
        // if socket is connected
        if (events & EPOLLIN) {
            signals |= MX_SOCKET_READABLE | MX_SOCKET_PEER_CLOSED;
        }
        if (events & EPOLLOUT) {
            signals |= MX_SOCKET_WRITABLE;
        }
    } else {
        // if socket is not connected
        if (events & EPOLLIN) {
            // signal when a listening socket gets an incoming connection
            // or a connecting socket gets connected and receives data
            signals |= MXSIO_SIGNAL_INCOMING |
                MX_SOCKET_READABLE | MX_SOCKET_PEER_CLOSED;
        }
        if (events & EPOLLOUT) {
            // signal when connect() operation is finished
            signals |= MXSIO_SIGNAL_OUTGOING;
        }
    }
    if (events & EPOLLRDHUP) {
        signals |= MX_SOCKET_PEER_CLOSED;
    }
    *_signals = signals;
}

static void mxsio_wait_end_stream(mxio_t* io, mx_signals_t signals, uint32_t* _events) {
    // check the connection state
    if (io->flags & MXIO_FLAG_SOCKET_CONNECTING) {
        if (signals & MXSIO_SIGNAL_CONNECTED) {
            io->flags &= ~MXIO_FLAG_SOCKET_CONNECTING;
            io->flags |= MXIO_FLAG_SOCKET_CONNECTED;
        }
    }
    uint32_t events = 0;
    if (io->flags & MXIO_FLAG_SOCKET_CONNECTED) {
        if (signals & (MX_SOCKET_READABLE | MX_SOCKET_PEER_CLOSED)) {
            events |= EPOLLIN;
        }
        if (signals & MX_SOCKET_WRITABLE) {
            events |= EPOLLOUT;
        }
    } else {
        if (signals & (MXSIO_SIGNAL_INCOMING | MX_SOCKET_PEER_CLOSED)) {
            events |= EPOLLIN;
        }
        if (signals & MXSIO_SIGNAL_OUTGOING) {
            events |= EPOLLOUT;
        }
    }
    if (signals & MXSIO_SIGNAL_ERROR) {
        events |= EPOLLERR;
    }
    if (signals & MX_SOCKET_PEER_CLOSED) {
        events |= EPOLLRDHUP;
    }
    *_events = events;
}

static ssize_t mxsio_posix_ioctl_stream(mxio_t* io, int req, va_list va) {
    mxrio_t* rio = (mxrio_t*)io;
    switch (req) {
    case FIONREAD: {
        mx_status_t r;
        size_t avail;
        if ((r = mx_socket_read(rio->h2, 0, NULL, 0, &avail)) < 0) {
            return r;
        }
        if (avail > INT_MAX) {
            avail = INT_MAX;
        }
        int* actual = va_arg(va, int*);
        *actual = avail;
        return NO_ERROR;
    }
    default:
        return ERR_NOT_SUPPORTED;
    }
}

static ssize_t mxsio_rx_dgram(mxio_t* io, void* buf, size_t buflen) {
    size_t n = 0;
    for (;;) {
        ssize_t r;
        mxrio_t* rio = (mxrio_t*)io;
        // TODO: if mx_socket support dgram mode, we'll switch to it
        if ((r = mx_channel_read(rio->h2, 0, buf, NULL, buflen,
                                 0, (uint32_t*)&n, NULL)) == NO_ERROR) {
            return n;
        }
        if (r == ERR_PEER_CLOSED) {
            return 0;
        } else if (r == ERR_SHOULD_WAIT) {
            if (io->flags & MXIO_FLAG_NONBLOCK) {
                return r;
            }
            mx_signals_t pending;
            r = mx_object_wait_one(rio->h2,
                                   MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED,
                                   MX_TIME_INFINITE, &pending);
            if (r < 0) {
                return r;
            }
            if (pending & MX_CHANNEL_READABLE) {
                continue;
            }
            if (pending & MX_CHANNEL_PEER_CLOSED) {
                return 0;
            }
            // impossible
            return ERR_INTERNAL;
        }
        return (ssize_t)n;
    }
}

static ssize_t mxsio_tx_dgram(mxio_t* io, const void* buf, size_t buflen) {
    mxrio_t* rio = (mxrio_t*)io;
    // TODO: mx_channel_write never returns ERR_SHOULD_WAIT, which is a problem.
    // if mx_socket supports dgram mode, we'll switch to it.
    return mx_channel_write(rio->h2, 0, buf, buflen, NULL, 0);
}

static ssize_t mxsio_recvmsg_dgram(mxio_t* io, struct msghdr* msg, int flags);
static ssize_t mxsio_sendmsg_dgram(mxio_t* io, const struct msghdr* msg, int flags);

static ssize_t mxsio_read_dgram(mxio_t* io, void* data, size_t len) {
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

    return mxsio_recvmsg_dgram(io, &msg, 0);
}

static ssize_t mxsio_write_dgram(mxio_t* io, const void* data, size_t len) {
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

    return mxsio_sendmsg_dgram(io, &msg, 0);
}

static ssize_t mxsio_recvmsg_dgram(mxio_t* io, struct msghdr* msg, int flags) {
    if (flags != 0) {
        // TODO: support MSG_OOB
        return ERR_NOT_SUPPORTED;
    }
    // TODO: support flags and control messages
    if (io->flags & MXIO_FLAG_SOCKET_CONNECTED) {
        // if connected, can't specify address
        if (msg->msg_name != NULL || msg->msg_namelen != 0) {
            return ERR_ALREADY_EXISTS;
        }
    }
    size_t mlen = 0;
    for (int i = 0; i < msg->msg_iovlen; i++) {
        struct iovec *iov = &msg->msg_iov[i];
        if (iov->iov_len <= 0) {
            return ERR_INVALID_ARGS;
        }
        mlen += iov->iov_len;
    }
    mlen += MXIO_SOCKET_MSG_HEADER_SIZE;

    // TODO: avoid malloc
    mxio_socket_msg_t* m = malloc(mlen);
    ssize_t n = mxsio_rx_dgram(io, m, mlen);
    if (n < 0) {
        free(m);
        return n;
    }
    if ((size_t)n < MXIO_SOCKET_MSG_HEADER_SIZE) {
        free(m);
        return ERR_INTERNAL;
    }
    n -= MXIO_SOCKET_MSG_HEADER_SIZE;
    if (msg->msg_name != NULL) {
        memcpy(msg->msg_name, &m->addr, m->addrlen);
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
    free(m);
    return n;
}

static ssize_t mxsio_sendmsg_dgram(mxio_t* io, const struct msghdr* msg, int flags) {
    if (flags != 0) {
        // TODO: MSG_OOB
        return ERR_NOT_SUPPORTED;
    }
    // TODO: support flags and control messages
    if (io->flags & MXIO_FLAG_SOCKET_CONNECTED) {
        // if connected, can't specify address
        if (msg->msg_name != NULL || msg->msg_namelen != 0) {
            return ERR_ALREADY_EXISTS;
        }
    }
    ssize_t n = 0;
    for (int i = 0; i < msg->msg_iovlen; i++) {
        struct iovec *iov = &msg->msg_iov[i];
        if (iov->iov_len <= 0) {
            return ERR_INVALID_ARGS;
        }
        n += iov->iov_len;
    }
    size_t mlen = n + MXIO_SOCKET_MSG_HEADER_SIZE;

    // TODO: avoid malloc m
    mxio_socket_msg_t* m = malloc(mlen);
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
    ssize_t r = mxsio_tx_dgram(io, m, mlen);
    free(m);
    return r == NO_ERROR ? n : r;
}

static void mxsio_wait_begin_dgram(mxio_t* io, uint32_t events, mx_handle_t* handle, mx_signals_t* _signals) {
    mxrio_t* rio = (void*)io;
    *handle = rio->h2;
    mx_signals_t signals = MXSIO_SIGNAL_ERROR;
    if (events & EPOLLIN) {
        signals |= MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED;
    }
    if (events & EPOLLOUT) {
        signals |= MX_CHANNEL_WRITABLE;
    }
    if (events & EPOLLRDHUP) {
        signals |= MX_CHANNEL_PEER_CLOSED;
    }
    *_signals = signals;
}

static void mxsio_wait_end_dgram(mxio_t* io, mx_signals_t signals, uint32_t* _events) {
    uint32_t events = 0;
    if (signals & (MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED)) {
        events |= EPOLLIN;
    }
    if (signals & MX_CHANNEL_WRITABLE) {
        events |= EPOLLOUT;
    }
    if (signals & MXSIO_SIGNAL_ERROR) {
        events |= EPOLLERR;
    }
    if (signals & MX_CHANNEL_PEER_CLOSED) {
        events |= EPOLLRDHUP;
    }
    *_events = events;
}

static mxio_ops_t mxio_socket_stream_ops = {
    .read = mxsio_read_stream,
    .write = mxsio_write_stream,
    .recvmsg = mxsio_recvmsg_stream,
    .sendmsg = mxsio_sendmsg_stream,
    .seek = mxio_default_seek,
    .misc = mxrio_misc,
    .close = mxrio_close,
    .open = mxrio_open,
    .clone = mxsio_clone_stream,
    .ioctl = mxrio_ioctl,
    .wait_begin = mxsio_wait_begin_stream,
    .wait_end = mxsio_wait_end_stream,
    .unwrap = mxsio_unwrap_stream,
    .posix_ioctl = mxsio_posix_ioctl_stream,
    .get_vmo = mxio_default_get_vmo,
};

static mxio_ops_t mxio_socket_dgram_ops = {
    .read = mxsio_read_dgram,
    .write = mxsio_write_dgram,
    .recvmsg = mxsio_recvmsg_dgram,
    .sendmsg = mxsio_sendmsg_dgram,
    .seek = mxio_default_seek,
    .misc = mxrio_misc,
    .close = mxrio_close,
    .open = mxrio_open,
    .clone = mxio_default_clone,
    .ioctl = mxrio_ioctl,
    .wait_begin = mxsio_wait_begin_dgram,
    .wait_end = mxsio_wait_end_dgram,
    .unwrap = mxio_default_unwrap,
    .posix_ioctl = mxio_default_posix_ioctl, // not supported
    .get_vmo = mxio_default_get_vmo,
};

mxio_t* mxio_socket_create(mx_handle_t h, mx_handle_t s, int flags) {
    mxrio_t* rio = calloc(1, sizeof(*rio));
    if (rio == NULL) {
        mx_handle_close(h);
        mx_handle_close(s);
        return NULL;
    }
    rio->io.ops = &mxio_socket_stream_ops; // default is stream
    rio->io.magic = MXIO_MAGIC;
    rio->io.refcount = 1;
    rio->io.flags = MXIO_FLAG_SOCKET | flags;
    rio->h = h;
    rio->h2 = s;
    return &rio->io;
}

void mxio_socket_set_stream_ops(mxio_t* io) {
    mxrio_t* rio = (mxrio_t*)io;
    rio->io.ops = &mxio_socket_stream_ops;
}

void mxio_socket_set_dgram_ops(mxio_t* io) {
    mxrio_t* rio = (mxrio_t*)io;
    rio->io.ops = &mxio_socket_dgram_ops;
}

mx_status_t mxio_socket_shutdown(mxio_t* io, int how) {
    mxrio_t* rio = (mxrio_t*)io;
    if (how == SHUT_RD || how == SHUT_RDWR) {
        // TODO: turn on a flag to prevent all read attempts
    }
    if (how == SHUT_WR || how == SHUT_RDWR) {
        // TODO: turn on a flag to prevent all write attempts
        mx_object_signal_peer(rio->h2, 0u, MXSIO_SIGNAL_HALFCLOSED);
    }
    return NO_ERROR;
}
