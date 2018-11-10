// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <poll.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include <lib/fdio/io.h>
#include <lib/fdio/remoteio.h>
#include <lib/fdio/util.h>
#include <lib/zxs/protocol.h>

#include "private-socket.h"
#include "unistd.h"

static void update_blocking_flag(zxsio_t* sio) {
    // Ideally, we would keep the blocking state of the embedded
    // |zxs_socket_t| up to date as the IOFLAG_NONBLOCK state of the ioflag
    // changes, but that would involve changing generic code in unistd.c.
    // For now, we update the |zxs_socket_t| flag state lazily here.
    if (sio->io.ioflag & IOFLAG_NONBLOCK) {
        sio->s.flags &= ~ZXS_FLAG_BLOCKING;
    } else {
        sio->s.flags |= ZXS_FLAG_BLOCKING;
    }
}

static ssize_t zxsio_read(fdio_t* io, void* data, size_t len) {
    zxsio_t* sio = (void*)io;
    update_blocking_flag(sio);
    size_t actual = 0u;
    zx_status_t status = zxs_recv(&sio->s, data, len, &actual);
    return status != ZX_OK ? status : (ssize_t)actual;
}

static ssize_t zxsio_write(fdio_t* io, const void* data, size_t len) {
    zxsio_t* sio = (void*)io;
    update_blocking_flag(sio);
    size_t actual = 0u;
    zx_status_t status = zxs_send(&sio->s, data, len, &actual);
    return status != ZX_OK ? status : (ssize_t)actual;
}

static ssize_t zxsio_recvfrom(fdio_t* io, void* data, size_t len, int flags,
                              struct sockaddr* restrict addr,
                              socklen_t* restrict addrlen) {
    zxsio_t* sio = (void*)io;
    update_blocking_flag(sio);
    size_t addr_actual = 0u;
    size_t actual = 0u;
    zx_status_t status = zxs_recvfrom(&sio->s, addr, addrlen != NULL ? *addrlen : 0u,
                                      &addr_actual, data, len, &actual);
    if (status == ZX_OK) {
        if (addrlen != NULL) {
            *addrlen = addr_actual;
        }
        return (ssize_t)actual;
    }
    return status;
}

static ssize_t zxsio_sendto(fdio_t* io, const void* data, size_t len, int flags,
                            const struct sockaddr* addr, socklen_t addrlen) {
    zxsio_t* sio = (void*)io;
    update_blocking_flag(sio);
    size_t actual = 0u;
    zx_status_t status = zxs_sendto(&sio->s, addr, addrlen, data, len, &actual);
    return status != ZX_OK ? status : (ssize_t)actual;
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

    zxsio_t* sio = (void*)io;
    update_blocking_flag(sio);
    size_t actual = 0u;
    zx_status_t status = zxs_recvmsg(&sio->s, msg, &actual);
    return status != ZX_OK ? status : (ssize_t)actual;
}

static ssize_t zxsio_sendmsg_stream(fdio_t* io, const struct msghdr* msg, int flags) {
    if (flags != 0) {
        // TODO: support MSG_NOSIGNAL
        // TODO: support MSG_OOB
        return ZX_ERR_NOT_SUPPORTED;
    }
    // TODO: support flags and control messages
    if (io->ioflag & IOFLAG_SOCKET_CONNECTED) {
        // if connected, can't specify address different than remote endpoint.
        if (msg->msg_name != NULL || msg->msg_namelen != 0) {
            return ZX_ERR_ALREADY_EXISTS;
        }
    } else {
        return ZX_ERR_BAD_STATE;
    }

    zxsio_t* sio = (void*)io;
    update_blocking_flag(sio);
    size_t actual = 0u;
    zx_status_t status = zxs_sendmsg(&sio->s, msg, &actual);
    return status != ZX_OK ? status : (ssize_t)actual;
}

static zx_status_t zxsio_clone(fdio_t* io, zx_handle_t* handles, uint32_t* types) {
    // TODO: support unconnected sockets
    if (!(io->ioflag & IOFLAG_SOCKET_CONNECTED)) {
        return ZX_ERR_BAD_STATE;
    }
    zxsio_t* sio = (void*)io;
    zx_status_t r = zx_handle_duplicate(sio->s.socket, ZX_RIGHT_SAME_RIGHTS, handles);
    if (r < 0) {
        return r;
    }
    types[0] = PA_FDIO_SOCKET;
    return 1;
}

static zx_status_t zxsio_unwrap(fdio_t* io, zx_handle_t* handles, uint32_t* types) {
    // TODO: support unconnected sockets
    if (!(io->ioflag & IOFLAG_SOCKET_CONNECTED)) {
        return ZX_ERR_BAD_STATE;
    }
    zxsio_t* sio = (void*)io;
    zx_status_t r;
    handles[0] = sio->s.socket;
    types[0] = PA_FDIO_SOCKET;
    r = 1;
    return r;
}

static void zxsio_wait_begin_stream(fdio_t* io, uint32_t events, zx_handle_t* handle, zx_signals_t* _signals) {
    zxsio_t* sio = (void*)io;
    *handle = sio->s.socket;
    // TODO: locking for flags/state
    if (io->ioflag & IOFLAG_SOCKET_CONNECTING) {
        // check the connection state
        zx_signals_t observed;
        zx_status_t r;
        r = zx_object_wait_one(sio->s.socket, ZXSIO_SIGNAL_CONNECTED, 0u,
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
            signals |= ZX_SOCKET_READABLE | ZX_SOCKET_PEER_WRITE_DISABLED | ZX_SOCKET_PEER_CLOSED;
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
                       ZX_SOCKET_READABLE | ZX_SOCKET_PEER_WRITE_DISABLED | ZX_SOCKET_PEER_CLOSED;
        }
        if (events & POLLOUT) {
            // signal when connect() operation is finished
            signals |= ZXSIO_SIGNAL_OUTGOING;
        }
    }
    if (events & POLLRDHUP) {
        signals |= ZX_SOCKET_PEER_WRITE_DISABLED | ZX_SOCKET_PEER_CLOSED;
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
        if (signals & (ZX_SOCKET_READABLE | ZX_SOCKET_PEER_WRITE_DISABLED | ZX_SOCKET_PEER_CLOSED)) {
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
    if (signals & (ZX_SOCKET_PEER_WRITE_DISABLED | ZX_SOCKET_PEER_CLOSED)) {
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
        if ((r = zx_socket_read(sio->s.socket, 0, NULL, 0, &avail)) < 0) {
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

static ssize_t zxsio_recvmsg_dgram(fdio_t* io, struct msghdr* msg, int flags) {
    if (flags != 0) {
        // TODO: support MSG_OOB
        return ZX_ERR_NOT_SUPPORTED;
    }
    zxsio_t* sio = (void*)io;
    update_blocking_flag(sio);
    size_t actual = 0u;
    zx_status_t status = zxs_recvmsg(&sio->s, msg, &actual);
    return status != ZX_OK ? status : (ssize_t)actual;
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
    zxsio_t* sio = (void*)io;
    update_blocking_flag(sio);
    size_t actual = 0u;
    zx_status_t status = zxs_sendmsg(&sio->s, msg, &actual);
    return status != ZX_OK ? status : (ssize_t)actual;
}

static void zxsio_wait_begin_dgram(fdio_t* io, uint32_t events, zx_handle_t* handle, zx_signals_t* _signals) {
    zxsio_t* sio = (void*)io;
    *handle = sio->s.socket;
    zx_signals_t signals = ZXSIO_SIGNAL_ERROR;
    if (events & POLLIN) {
        signals |= ZX_SOCKET_READABLE | ZX_SOCKET_PEER_WRITE_DISABLED | ZX_SOCKET_PEER_CLOSED;
    }
    if (events & POLLOUT) {
        signals |= ZX_SOCKET_WRITABLE | ZX_SOCKET_WRITE_DISABLED;
    }
    if (events & POLLRDHUP) {
        signals |= ZX_SOCKET_PEER_WRITE_DISABLED | ZX_SOCKET_PEER_CLOSED;
    }
    *_signals = signals;
}

static void zxsio_wait_end_dgram(fdio_t* io, zx_signals_t signals, uint32_t* _events) {
    uint32_t events = 0;
    if (signals & (ZX_SOCKET_READABLE | ZX_SOCKET_PEER_WRITE_DISABLED | ZX_SOCKET_PEER_CLOSED)) {
        events |= POLLIN;
    }
    if (signals & (ZX_SOCKET_WRITABLE | ZX_SOCKET_WRITE_DISABLED)) {
        events |= POLLOUT;
    }
    if (signals & ZXSIO_SIGNAL_ERROR) {
        events |= POLLERR;
    }
    if (signals & (ZX_SOCKET_PEER_WRITE_DISABLED | ZX_SOCKET_PEER_CLOSED)) {
        events |= POLLRDHUP;
    }
    *_events = events;
}

static zx_status_t zxsio_close(fdio_t* io) {
    zxsio_t* sio = (zxsio_t*)io;
    update_blocking_flag(sio);
    return zxs_close(&sio->s);
}

static ssize_t zxsio_ioctl(fdio_t* io, uint32_t op, const void* in_buf,
                           size_t in_len, void* out_buf, size_t out_len) {
    zxsio_t* sio = (void*)io;
    size_t actual = 0u;
    zx_status_t status = zxs_ioctl(&sio->s, op, in_buf, in_len, out_buf,
                                   out_len, &actual);
    return status != ZX_OK ? status : (ssize_t)actual;
}

static zx_status_t fdio_socket_shutdown(fdio_t* io, int how) {
    if (!(io->ioflag & IOFLAG_SOCKET_CONNECTED)) {
        return ZX_ERR_BAD_STATE;
    }
    zxsio_t* sio = (zxsio_t*)io;
    if (how == SHUT_WR || how == SHUT_RDWR) {
        // netstack expects this user signal to be set - raise it to keep that code working until
        // it learns about the read/write disabled signals.
        zx_object_signal_peer(sio->s.socket, 0u, ZXSIO_SIGNAL_HALFCLOSED);
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
    return zx_socket_shutdown(sio->s.socket, options);
}

static fdio_ops_t fdio_socket_stream_ops = {
    .read = zxsio_read,
    .read_at = fdio_default_read_at,
    .write = zxsio_write,
    .write_at = fdio_default_write_at,
    .seek = fdio_default_seek,
    .misc = fdio_default_misc,
    .close = zxsio_close,
    .open = fdio_default_open,
    .clone = zxsio_clone,
    .ioctl = zxsio_ioctl,
    .wait_begin = zxsio_wait_begin_stream,
    .wait_end = zxsio_wait_end_stream,
    .unwrap = zxsio_unwrap,
    .posix_ioctl = zxsio_posix_ioctl_stream,
    .get_vmo = fdio_default_get_vmo,
    .get_token = fdio_default_get_token,
    .get_attr = fdio_default_get_attr,
    .set_attr = fdio_default_set_attr,
    .sync = fdio_default_sync,
    .readdir = fdio_default_readdir,
    .rewind = fdio_default_rewind,
    .unlink = fdio_default_unlink,
    .truncate = fdio_default_truncate,
    .rename = fdio_default_rename,
    .link = fdio_default_link,
    .get_flags = fdio_default_get_flags,
    .set_flags = fdio_default_set_flags,
    .recvfrom = zxsio_recvfrom,
    .sendto = zxsio_sendto,
    .recvmsg = zxsio_recvmsg_stream,
    .sendmsg = zxsio_sendmsg_stream,
    .shutdown = fdio_socket_shutdown,
};

static fdio_ops_t fdio_socket_dgram_ops = {
    .read = zxsio_read,
    .read_at = fdio_default_read_at,
    .write = zxsio_write,
    .write_at = fdio_default_write_at,
    .seek = fdio_default_seek,
    .misc = fdio_default_misc,
    .close = zxsio_close,
    .open = fdio_default_open,
    .clone = zxsio_clone,
    .ioctl = zxsio_ioctl,
    .wait_begin = zxsio_wait_begin_dgram,
    .wait_end = zxsio_wait_end_dgram,
    .unwrap = zxsio_unwrap,
    .posix_ioctl = fdio_default_posix_ioctl, // not supported
    .get_vmo = fdio_default_get_vmo,
    .get_token = fdio_default_get_token,
    .get_attr = fdio_default_get_attr,
    .set_attr = fdio_default_set_attr,
    .sync = fdio_default_sync,
    .readdir = fdio_default_readdir,
    .rewind = fdio_default_rewind,
    .unlink = fdio_default_unlink,
    .truncate = fdio_default_truncate,
    .rename = fdio_default_rename,
    .link = fdio_default_link,
    .get_flags = fdio_default_get_flags,
    .set_flags = fdio_default_set_flags,
    .recvfrom = zxsio_recvfrom,
    .sendto = zxsio_sendto,
    .recvmsg = zxsio_recvmsg_dgram,
    .sendmsg = zxsio_sendmsg_dgram,
    .shutdown = fdio_socket_shutdown,
};

static fdio_t* fdio_socket_create(zx_handle_t s, int flags, fdio_ops_t* ops) {
    zxsio_t* sio = calloc(1, sizeof(*sio));
    if (sio == NULL) {
        zx_handle_close(s);
        return NULL;
    }
    sio->io.ops = ops;
    sio->io.magic = FDIO_MAGIC;
    sio->io.refcount = 1;
    sio->io.ioflag = IOFLAG_SOCKET | flags;
    sio->s.socket = s;
    sio->s.flags = ops == &fdio_socket_dgram_ops ? ZXS_FLAG_DATAGRAM : 0u;
    sio->flags = 0;
    return &sio->io;
}

fdio_t* fdio_socket_create_stream(zx_handle_t s, int flags) {
    return fdio_socket_create(s, flags, &fdio_socket_stream_ops);
}

fdio_t* fdio_socket_create_datagram(zx_handle_t s, int flags) {
    return fdio_socket_create(s, flags, &fdio_socket_dgram_ops);
}

fdio_t* fd_to_socket(int fd, const zxs_socket_t** out_socket) {
    fdio_t* io = fd_to_io(fd);
    if (io == NULL) {
        *out_socket = NULL;
        return NULL;
    }

    if (io->ops == &fdio_socket_stream_ops
        || io->ops == &fdio_socket_dgram_ops) {
        zxsio_t* sio = (zxsio_t*)io;
        update_blocking_flag(sio);
        *out_socket = &sio->s;
        return io;
    }

    fdio_release(io);
    *out_socket = NULL;
    return NULL;
}
