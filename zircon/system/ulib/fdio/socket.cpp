// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/io.h>
#include <lib/zxio/inception.h>
#include <lib/zxs/protocol.h>
#include <poll.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include "private-socket.h"
#include "private.h"
#include "unistd.h"

static inline zxio_socket_t* fdio_get_zxio_socket(fdio_t* io) {
    return reinterpret_cast<zxio_socket_t*>(fdio_get_zxio(io));
}

static ssize_t zxsio_recvfrom(fdio_t* io, void* data, size_t len, int flags,
                              struct sockaddr* __restrict addr,
                              socklen_t* __restrict addrlen) {
    zxio_socket_t* sio = fdio_get_zxio_socket(io);
    size_t addr_actual = 0u;
    size_t actual = 0u;
    zx_status_t status = zxs_recvfrom(&sio->socket, addr, addrlen != NULL ? *addrlen : 0u,
                                      &addr_actual, data, len, &actual);
    if (status == ZX_OK) {
        if (addrlen != NULL) {
            *addrlen = static_cast<socklen_t>(addr_actual);
        }
        return static_cast<ssize_t>(actual);
    }
    return status;
}

static ssize_t zxsio_sendto(fdio_t* io, const void* data, size_t len, int flags,
                            const struct sockaddr* addr, socklen_t addrlen) {
    zxio_socket_t* sio = fdio_get_zxio_socket(io);
    size_t actual = 0u;
    zx_status_t status = zxs_sendto(&sio->socket, addr, addrlen, data, len, &actual);
    return status != ZX_OK ? status : static_cast<ssize_t>(actual);
}

static ssize_t zxsio_recvmsg_stream(fdio_t* io, struct msghdr* msg, int flags) {
    if (flags != 0) {
        // TODO: support MSG_OOB
        return ZX_ERR_NOT_SUPPORTED;
    }
    if (!(*fdio_get_ioflag(io) & IOFLAG_SOCKET_CONNECTED)) {
        return ZX_ERR_BAD_STATE;
    }
    // we ignore msg_name and msg_namelen members.
    // (this is a consistent behavior with other OS implementations for TCP protocol)

    zxio_socket_t* sio = fdio_get_zxio_socket(io);
    size_t actual = 0u;
    zx_status_t status = zxs_recvmsg(&sio->socket, msg, &actual);
    return status != ZX_OK ? status : static_cast<ssize_t>(actual);
}

static ssize_t zxsio_sendmsg_stream(fdio_t* io, const struct msghdr* msg, int flags) {
    if (flags != 0) {
        // TODO: support MSG_NOSIGNAL
        // TODO: support MSG_OOB
        return ZX_ERR_NOT_SUPPORTED;
    }
    // TODO: support flags and control messages
    if (*fdio_get_ioflag(io) & IOFLAG_SOCKET_CONNECTED) {
        // if connected, can't specify address different than remote endpoint.
        if (msg->msg_name != NULL || msg->msg_namelen != 0) {
            return ZX_ERR_ALREADY_EXISTS;
        }
    } else {
        return ZX_ERR_BAD_STATE;
    }

    zxio_socket_t* sio = fdio_get_zxio_socket(io);
    size_t actual = 0u;
    zx_status_t status = zxs_sendmsg(&sio->socket, msg, &actual);
    return status != ZX_OK ? status : static_cast<ssize_t>(actual);
}

static zx_status_t zxsio_clone(fdio_t* io, zx_handle_t* out_handle) {
    // TODO: support unconnected sockets
    if (!(*fdio_get_ioflag(io) & IOFLAG_SOCKET_CONNECTED)) {
        return ZX_ERR_BAD_STATE;
    }
    zxio_socket_t* sio = fdio_get_zxio_socket(io);
    return zx_handle_duplicate(sio->socket.socket, ZX_RIGHT_SAME_RIGHTS, out_handle);
}

static zx_status_t zxsio_unwrap(fdio_t* io, zx_handle_t* out_handle) {
    // TODO: support unconnected sockets
    if (!(*fdio_get_ioflag(io) & IOFLAG_SOCKET_CONNECTED)) {
        return ZX_ERR_BAD_STATE;
    }
    zxio_socket_t* sio = fdio_get_zxio_socket(io);
    *out_handle = sio->socket.socket;
    return ZX_OK;
}

static void zxsio_wait_begin_stream(fdio_t* io, uint32_t events, zx_handle_t* handle, zx_signals_t* _signals) {
    zxio_socket_t* sio = fdio_get_zxio_socket(io);
    *handle = sio->socket.socket;
    // TODO: locking for flags/state
    if (*fdio_get_ioflag(io) & IOFLAG_SOCKET_CONNECTING) {
        // check the connection state
        zx_signals_t observed;
        zx_status_t r;
        r = zx_object_wait_one(sio->socket.socket, ZXSIO_SIGNAL_CONNECTED, 0u,
                               &observed);
        if (r == ZX_OK || r == ZX_ERR_TIMED_OUT) {
            if (observed & ZXSIO_SIGNAL_CONNECTED) {
                *fdio_get_ioflag(io) &= ~IOFLAG_SOCKET_CONNECTING;
                *fdio_get_ioflag(io) |= IOFLAG_SOCKET_CONNECTED;
            }
        }
    }
    zx_signals_t signals = ZXSIO_SIGNAL_ERROR;
    if (*fdio_get_ioflag(io) & IOFLAG_SOCKET_CONNECTED) {
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
            signals |= ZXSIO_SIGNAL_INCOMING | ZX_SOCKET_ACCEPT |
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
    if (*fdio_get_ioflag(io) & IOFLAG_SOCKET_CONNECTING) {
        if (signals & ZXSIO_SIGNAL_CONNECTED) {
            *fdio_get_ioflag(io) &= ~IOFLAG_SOCKET_CONNECTING;
            *fdio_get_ioflag(io) |= IOFLAG_SOCKET_CONNECTED;
        }
    }
    uint32_t events = 0;
    if (*fdio_get_ioflag(io) & IOFLAG_SOCKET_CONNECTED) {
        if (signals & (ZX_SOCKET_READABLE | ZX_SOCKET_PEER_WRITE_DISABLED | ZX_SOCKET_PEER_CLOSED)) {
            events |= POLLIN;
        }
        if (signals & (ZX_SOCKET_WRITABLE | ZX_SOCKET_WRITE_DISABLED)) {
            events |= POLLOUT;
        }
    } else {
        if (signals & (ZXSIO_SIGNAL_INCOMING | ZX_SOCKET_ACCEPT | ZX_SOCKET_PEER_CLOSED)) {
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
    zxio_socket_t* sio = fdio_get_zxio_socket(io);
    switch (req) {
    case FIONREAD: {
        zx_info_socket_t info;
        memset(&info, 0, sizeof(info));
        zx_status_t status = zx_object_get_info(sio->socket.socket, ZX_INFO_SOCKET,
                                                &info, sizeof(info), NULL, NULL);
        if (status != ZX_OK) {
            return status;
        }
        size_t available = info.rx_buf_available;
        if (available > INT_MAX) {
            available = INT_MAX;
        }
        int* actual = va_arg(va, int*);
        *actual = static_cast<int>(available);
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
    zxio_socket_t* sio = fdio_get_zxio_socket(io);
    size_t actual = 0u;
    zx_status_t status = zxs_recvmsg(&sio->socket, msg, &actual);
    return status != ZX_OK ? status : static_cast<ssize_t>(actual);
}

static ssize_t zxsio_sendmsg_dgram(fdio_t* io, const struct msghdr* msg, int flags) {
    if (flags != 0) {
        // TODO: MSG_OOB
        return ZX_ERR_NOT_SUPPORTED;
    }
    // TODO: support flags and control messages
    if (*fdio_get_ioflag(io) & IOFLAG_SOCKET_CONNECTED) {
        // if connected, can't specify address
        if (msg->msg_name != NULL || msg->msg_namelen != 0) {
            return ZX_ERR_ALREADY_EXISTS;
        }
    }
    zxio_socket_t* sio = fdio_get_zxio_socket(io);
    size_t actual = 0u;
    zx_status_t status = zxs_sendmsg(&sio->socket, msg, &actual);
    return status != ZX_OK ? status : static_cast<ssize_t>(actual);
}

static void zxsio_wait_begin_dgram(fdio_t* io, uint32_t events, zx_handle_t* handle, zx_signals_t* _signals) {
    zxio_socket_t* sio = fdio_get_zxio_socket(io);
    *handle = sio->socket.socket;
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
    zxio_socket_t* sio = fdio_get_zxio_socket(io);
    return zxs_close(&sio->socket);
}

static ssize_t zxsio_ioctl(fdio_t* io, uint32_t op, const void* in_buf,
                           size_t in_len, void* out_buf, size_t out_len) {
    zxio_socket_t* sio = fdio_get_zxio_socket(io);
    int16_t out_code;
    size_t actual;
    zx_status_t status = fuchsia_net_SocketControlIoctl(
        sio->socket.socket, static_cast<uint16_t>(op),
        static_cast<const uint8_t*>(in_buf), in_len, &out_code,
        static_cast<uint8_t*>(out_buf), out_len, &actual);
    if (status != ZX_OK) {
        return status;
    }
    if (out_code) {
        return errno_to_fdio_status(out_code);
    }
    return static_cast<ssize_t>(actual);
}

static zx_status_t fdio_socket_shutdown(fdio_t* io, int how) {
    if (!(*fdio_get_ioflag(io) & IOFLAG_SOCKET_CONNECTED)) {
        return ZX_ERR_BAD_STATE;
    }
    zxio_socket_t* sio = fdio_get_zxio_socket(io);
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
    return zx_socket_shutdown(sio->socket.socket, options);
}

static zx_duration_t fdio_socket_get_rcvtimeo(fdio_t* io) {
    return fdio_get_zxio_socket(io)->socket.rcvtimeo;
}

static fdio_ops_t fdio_socket_stream_ops = {
    .close = zxsio_close,
    .open = fdio_default_open,
    .clone = zxsio_clone,
    .unwrap = zxsio_unwrap,
    .wait_begin = zxsio_wait_begin_stream,
    .wait_end = zxsio_wait_end_stream,
    .ioctl = zxsio_ioctl,
    .posix_ioctl = zxsio_posix_ioctl_stream,
    .get_vmo = fdio_default_get_vmo,
    .get_token = fdio_default_get_token,
    .get_attr = fdio_default_get_attr,
    .set_attr = fdio_default_set_attr,
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
    .get_rcvtimeo = fdio_socket_get_rcvtimeo,
};

static fdio_ops_t fdio_socket_dgram_ops = {
    .close = zxsio_close,
    .open = fdio_default_open,
    .clone = zxsio_clone,
    .unwrap = zxsio_unwrap,
    .wait_begin = zxsio_wait_begin_dgram,
    .wait_end = zxsio_wait_end_dgram,
    .ioctl = zxsio_ioctl,
    .posix_ioctl = fdio_default_posix_ioctl, // not supported
    .get_vmo = fdio_default_get_vmo,
    .get_token = fdio_default_get_token,
    .get_attr = fdio_default_get_attr,
    .set_attr = fdio_default_set_attr,
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
    .get_rcvtimeo = fdio_socket_get_rcvtimeo,
};

static fdio_t* fdio_socket_create(zx_handle_t socket, int flags,
                                  fdio_ops_t* ops) {
    fdio_t* io = fdio_alloc(ops);
    if (io == NULL) {
        zx_handle_close(socket);
        return NULL;
    }
    *fdio_get_ioflag(io) = IOFLAG_SOCKET | flags;
    zxs_socket_t zs = {
        .socket = socket,
        .flags = ops == &fdio_socket_dgram_ops ? ZXS_FLAG_DATAGRAM : 0u,
        .rcvtimeo = ZX_TIME_INFINITE,
    };
    zx_status_t status = zxio_socket_init(fdio_get_zxio_storage(io), zs);
    if (status != ZX_OK) {
        return NULL;
    }
    return io;
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

    if (fdio_get_ops(io) == &fdio_socket_stream_ops || fdio_get_ops(io) == &fdio_socket_dgram_ops) {
        zxio_socket_t* sio = fdio_get_zxio_socket(io);
        *out_socket = &sio->socket;
        return io;
    }

    fdio_release(io);
    *out_socket = NULL;
    return NULL;
}
