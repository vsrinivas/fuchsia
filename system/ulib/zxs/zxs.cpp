// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zxs/inception.h>
#include <lib/zxs/protocol.h>
#include <lib/zxs/zxs.h>
#include <string.h>
#include <zircon/syscalls.h>

static bool is_rio_message_valid(zxsio_msg_t* msg) {
    if ((msg->datalen > ZXSIO_PAYLOAD_SZ) ||
        (msg->hcount > 0)) {
        return false;
    }
    return true;
}

static bool is_rio_message_reply_valid(zxsio_msg_t* msg, size_t size) {
    if ((size < ZXSIO_HDR_SZ) ||
        (msg->datalen != (size - ZXSIO_HDR_SZ))) {
        return false;
    }
    return is_rio_message_valid(msg);
}

static ssize_t zxsio_write_control(zx_handle_t socket, zxsio_msg_t* msg) {
    for (;;) {
        ssize_t r;
        size_t len = ZXSIO_HDR_SZ + msg->datalen;
        if ((r = zx_socket_write(socket, ZX_SOCKET_CONTROL, msg, len, &len)) == ZX_OK) {
            return static_cast<ssize_t>(len);
        }
        // If the socket has no control plane then control messages are not
        // supported.
        if (r == ZX_ERR_BAD_STATE) {
            return ZX_ERR_NOT_SUPPORTED;
        }
        if (r == ZX_ERR_SHOULD_WAIT) {
            zx_signals_t pending;
            r = zx_object_wait_one(socket,
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

static ssize_t zxsio_read_control(zx_handle_t socket, void* data, size_t len) {
    // TODO: let the generic read() to do this loop
    for (;;) {
        ssize_t r;
        size_t bytes_read;
        if ((r = zx_socket_read(socket, ZX_SOCKET_CONTROL, data, len, &bytes_read)) == ZX_OK) {
            // zx_socket_read() sets *actual to the number of bytes in the buffer when data is NULL
            // and len is 0. read() should return 0 in that case.
            if (len == 0) {
                return 0;
            } else {
                return static_cast<ssize_t>(bytes_read);
            }
        }
        if (r == ZX_ERR_PEER_CLOSED || r == ZX_ERR_BAD_STATE) {
            return 0;
        } else if (r == ZX_ERR_SHOULD_WAIT) {
            zx_signals_t pending;
            r = zx_object_wait_one(socket,
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

zx_status_t zxsio_txn(zx_handle_t socket, zxsio_msg_t* msg) {
    if (!is_rio_message_valid(msg)) {
        return ZX_ERR_INVALID_ARGS;
    }

    ssize_t r = zxsio_write_control(socket, msg);
    if (r < 0)
        return static_cast<zx_status_t>(r);

    const uint32_t request_op = msg->op;
    r = zxsio_read_control(socket, msg, sizeof(*msg));
    if (r < 0)
        return static_cast<zx_status_t>(r);

    size_t dsize = (size_t)r;
    // check for protocol errors
    if (!is_rio_message_reply_valid(msg, dsize) || (msg->op != request_op)) {
        return ZX_ERR_IO;
    }
    return msg->arg;
}

zx_status_t zxsio_op(zx_handle_t socket, uint32_t op, int64_t off,
                     uint32_t maxreply, void* buffer, size_t length) {
    if ((length > ZXSIO_PAYLOAD_SZ) || (maxreply > ZXSIO_PAYLOAD_SZ)) {
        return ZX_ERR_INVALID_ARGS;
    }

    switch (op) {
    case ZXSIO_GETSOCKNAME:
    case ZXSIO_GETPEERNAME:
    case ZXSIO_GETSOCKOPT:
    case ZXSIO_SETSOCKOPT:
    case ZXSIO_CONNECT:
    case ZXSIO_BIND:
    case ZXSIO_LISTEN:
        break;
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }

    zxsio_msg_t msg;
    memset(&msg, 0, ZXSIO_HDR_SZ);
    msg.op = op;
    msg.arg = maxreply;
    msg.arg2.off = off;
    msg.datalen = static_cast<uint32_t>(length);
    if (buffer && length > 0) {
        memcpy(msg.data, buffer, length);
    }

    zx_status_t status = zxsio_txn(socket, &msg);
    if (status < 0) {
        return status;
    }
    if (msg.datalen > maxreply) {
        return ZX_ERR_IO;
    }
    if (buffer && msg.datalen > 0) {
        memcpy(buffer, msg.data, msg.datalen);
    }

    return status;
}

zx_status_t zxs_socket(zx_handle_t socket_provider,
                       fuchsia_net_SocketDomain domain,
                       fuchsia_net_SocketType type,
                       fuchsia_net_SocketProtocol protocol,
                       const zxs_option_t* options,
                       size_t options_count,
                       zxs_socket_t* out_socket) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t zxs_connect(const zxs_socket_t* socket, const struct sockaddr* addr,
                        size_t addr_length) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t zxs_bind(const zxs_socket_t* socket, const struct sockaddr* addr,
                     size_t addr_length) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t zxs_listen(const zxs_socket_t* socket, uint32_t backlog) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t zxs_accept(const zxs_socket_t* socket, const zxs_option_t* options,
                       size_t options_count, struct sockaddr* addr,
                       size_t addr_capacity, size_t* out_addr_actual,
                       zxs_socket_t* out_socket) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t zxs_getsockname(const zxs_socket_t* socket, struct sockaddr* addr,
                            size_t capacity, size_t* out_actual) {
    zxrio_sockaddr_reply_t reply = {};
    zx_status_t status = zxsio_op(socket->socket, ZXSIO_GETSOCKNAME, 0,
                                  sizeof(zxrio_sockaddr_reply_t), &reply,
                                  sizeof(reply));
    if (status != ZX_OK) {
        return status;
    }

    *out_actual = reply.len;
    memcpy(addr, &reply.addr, (capacity < reply.len) ? capacity : reply.len);
    return status;
}

zx_status_t zxs_getpeername(const zxs_socket_t* socket, struct sockaddr* addr,
                            size_t capacity, size_t* out_actual) {
    zxrio_sockaddr_reply_t reply = {};
    zx_status_t status = zxsio_op(socket->socket, ZXSIO_GETPEERNAME, 0,
                                  sizeof(zxrio_sockaddr_reply_t), &reply,
                                  sizeof(reply));
    if (status != ZX_OK) {
        return status;
    }

    *out_actual = reply.len;
    memcpy(addr, &reply.addr, (capacity < reply.len) ? capacity : reply.len);
    return status;
}

zx_status_t zxs_getsockopt(const zxs_socket_t* socket, int32_t level,
                           int32_t name, void* buffer, size_t capacity,
                           size_t* out_actual) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t zxs_setsockopts(zx_handle_t socket, zxs_option_t* options,
                            size_t count) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t zxs_send(const zxs_socket_t* socket, const void* buffer,
                     size_t capacity, size_t* out_actual) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t zxs_recv(const zxs_socket_t* socket, int flag, void* buffer,
                     size_t capacity, size_t* out_actual) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t zxs_sendto(const zxs_socket_t* socket, const struct sockaddr* addr,
                       size_t addr_length, const void* buffer, size_t capacity,
                       size_t* out_actual) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t zxs_recvfrom(const zxs_socket_t* socket, struct sockaddr* addr,
                        size_t addr_capacity, size_t* out_addr_actual,
                        void* buffer, size_t capacity, size_t* out_actual) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t zxs_sendmsg(const zxs_socket_t* socket, const struct msghdr* msg,
                        size_t* out_actual) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t zxs_recvmsg(const zxs_socket_t* socket, struct msghdr* msg,
                        size_t* out_actual) {
    return ZX_ERR_NOT_SUPPORTED;
}
