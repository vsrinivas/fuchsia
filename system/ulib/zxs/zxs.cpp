// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/net/c/fidl.h>
#include <lib/zx/socket.h>
#include <lib/zxs/protocol.h>
#include <lib/zxs/zxs.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/device/ioctl.h>
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

static zx_status_t zxsio_txn(zx_handle_t socket, zxsio_msg_t* msg) {
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

static zx_status_t zxsio_op(zx_handle_t socket, uint32_t op, int64_t off,
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
    zxs_socket_t socket = {};

    zx_status_t io_status, status;
    io_status = fuchsia_net_LegacySocketProviderOpenSocket(
        socket_provider, domain, type, protocol, &socket.socket, &status);

    if (io_status != ZX_OK) {
        return ZX_ERR_IO;
    }

    if (status != ZX_OK) {
        return status;
    }

    if (type == fuchsia_net_SocketType_dgram) {
        socket.flags |= ZXS_FLAG_DATAGRAM;
    }

    status = zxs_setsockopts(&socket, options, options_count);
    if (status != ZX_OK) {
        zxs_close(&socket);
        return status;
    }

    *out_socket = socket;
    return ZX_OK;
}

zx_status_t zxs_close(const zxs_socket_t* socket) {
    zxsio_msg_t msg;
    memset(&msg, 0, ZXSIO_HDR_SZ);
    msg.op = ZXSIO_CLOSE;
    zx_status_t status = zxsio_txn(socket->socket, &msg);
    zx_handle_close(socket->socket);
    return status;
}

zx_status_t zxs_connect(const zxs_socket_t* socket, const struct sockaddr* addr,
                        size_t addr_length) {
    zx_status_t status = zxsio_op(socket->socket, ZXSIO_CONNECT, 0, 0,
                                  const_cast<struct sockaddr*>(addr),
                                  addr_length);

    if (status == ZX_ERR_SHOULD_WAIT && (socket->flags & ZXS_FLAG_BLOCKING)) {
        zx_signals_t observed = ZX_SIGNAL_NONE;
        status = zx_object_wait_one(socket->socket, ZXSIO_SIGNAL_OUTGOING,
                                    ZX_TIME_INFINITE, &observed);
        if (status != ZX_OK) {
            return ZX_ERR_IO;
        }

        zx_status_t socket_status;
        size_t actual = 0u;
        status = zxs_getsockopt(socket, SOL_SOCKET, SO_ERROR, &socket_status,
                                sizeof(socket_status), &actual);
        if (status != ZX_OK) {
            return ZX_ERR_IO;
        }
        return socket_status;
    }

    return status;
}

zx_status_t zxs_bind(const zxs_socket_t* socket, const struct sockaddr* addr,
                     size_t addr_length) {
    return zxsio_op(socket->socket, ZXSIO_BIND, 0, 0,
                    const_cast<struct sockaddr*>(addr), addr_length);
}

zx_status_t zxs_listen(const zxs_socket_t* socket, uint32_t backlog) {
    return zxsio_op(socket->socket, ZXSIO_LISTEN, 0, 0, &backlog,
                    sizeof(backlog));
}

zx_status_t zxs_accept(const zxs_socket_t* socket, struct sockaddr* addr,
                       size_t addr_capacity, size_t* out_addr_actual,
                       zxs_socket_t* out_socket) {
    zx_status_t status = ZX_OK;
    zx::socket accepted;
    for (;;) {
        status = zx_socket_accept(socket->socket,
                                  accepted.reset_and_get_address());
        if (status == ZX_ERR_SHOULD_WAIT
            && (socket->flags & ZXS_FLAG_BLOCKING)) {

            zx_signals_t observed = ZX_SIGNAL_NONE;
            status = zx_object_wait_one(socket->socket, ZX_SOCKET_ACCEPT | ZX_SOCKET_PEER_CLOSED, ZX_TIME_INFINITE, &observed);
            if (status != ZX_OK) {
                break;
            }
            if (observed & ZX_SOCKET_ACCEPT) {
                continue;
            }
            if (observed & ZX_SOCKET_PEER_CLOSED) {
                return ZX_ERR_PEER_CLOSED;
            }
            // impossible
            return ZX_ERR_INTERNAL;
        }
        break;
    }

    if (status != ZX_OK) {
        return status;
    }

    zxs_socket_t accepted_socket = {
        .socket = accepted.release(),
        .flags = 0u,
    };

    status = zxs_getpeername(&accepted_socket, addr, addr_capacity, out_addr_actual);
    if (status != ZX_OK) {
        zx_handle_close(accepted_socket.socket);
        accepted_socket.socket = ZX_HANDLE_INVALID;
        return status;
    }

    *out_socket = accepted_socket;
    return ZX_OK;
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
    zxrio_sockopt_req_reply_t req_reply;
    memset(&req_reply, 0, sizeof(req_reply));
    req_reply.level = level;
    req_reply.optname = name;
    zx_status_t status = zxsio_op(socket->socket, ZXSIO_GETSOCKOPT, 0,
                                  sizeof(req_reply), &req_reply,
                                  sizeof(req_reply));
    if (status < 0) {
        return status;
    }
    size_t actual = (capacity < req_reply.optlen) ? capacity : req_reply.optlen;
    memcpy(buffer, req_reply.optval, actual);
    // Notice that |*out_actual| could be larger than |capacity| if the server
    // misbehaves. It would be safer to set |*out_actual| to |actual|.
    *out_actual = req_reply.optlen;
    return ZX_OK;
}

zx_status_t zxs_setsockopts(const zxs_socket_t* socket,
                            const zxs_option_t* options,
                            size_t count) {
    for (size_t i = 0u; i < count; ++i) {
        zxrio_sockopt_req_reply_t request;
        memset(&request, 0, sizeof(request));
        request.level = options[i].level;
        request.optname = options[i].name;
        size_t length = options[i].length;
        if (length > sizeof(request.optval)) {
            return ZX_ERR_INVALID_ARGS;
        }
        memcpy(request.optval, options[i].value, length);
        request.optlen = static_cast<socklen_t>(length);
        zx_status_t status = zxsio_op(socket->socket, ZXSIO_SETSOCKOPT, 0, 0,
                                      &request, sizeof(request));
        if (status != ZX_OK) {
            return status;
        }
    }
    return ZX_OK;
}

static zx_status_t zxs_write(const zxs_socket_t* socket, const void* buffer,
                             size_t capacity, size_t* out_actual) {
    for (;;) {
        zx_status_t status = zx_socket_write(socket->socket, 0, buffer,
                                             capacity, out_actual);
        if (status == ZX_ERR_SHOULD_WAIT && (socket->flags & ZXS_FLAG_BLOCKING)) {
            zx_signals_t observed = ZX_SIGNAL_NONE;
            status = zx_object_wait_one(socket->socket,
                                        ZX_SOCKET_WRITABLE | ZX_SOCKET_WRITE_DISABLED | ZX_SOCKET_PEER_CLOSED,
                                        ZX_TIME_INFINITE, &observed);
            if (status != ZX_OK) {
                return status;
            }
            if (observed & (ZX_SOCKET_WRITE_DISABLED | ZX_SOCKET_PEER_CLOSED)) {
                return ZX_ERR_PEER_CLOSED;
            }
            ZX_ASSERT(observed & ZX_SOCKET_WRITABLE);
            continue;
        }
        return status;
    }
}

static zx_status_t zxs_read(const zxs_socket_t* socket, void* buffer,
                            size_t capacity, size_t* out_actual) {
    for (;;) {
        zx_status_t status = zx_socket_read(socket->socket, 0, buffer, capacity,
                                            out_actual);
        if (status == ZX_OK) {
            if (capacity == 0u) {
                // zx_socket_read() sets *out_actual to the number of bytes in
                // the buffer when data is NULL and len is 0. zxio_read() should
                // return 0u in that case.
                *out_actual = 0u;
            }
            return ZX_OK;
        }
        if (status == ZX_ERR_PEER_CLOSED || status == ZX_ERR_BAD_STATE) {
            *out_actual = 0u;
            return ZX_OK;
        }
        if (status == ZX_ERR_SHOULD_WAIT && (socket->flags & ZXS_FLAG_BLOCKING)) {
            zx_signals_t observed = ZX_SIGNAL_NONE;
            status = zx_object_wait_one(socket->socket,
                                        ZX_SOCKET_READABLE | ZX_SOCKET_PEER_CLOSED | ZX_SOCKET_PEER_WRITE_DISABLED,
                                        ZX_TIME_INFINITE, &observed);
            if (status != ZX_OK) {
                return status;
            }
            if (observed & ZX_SOCKET_READABLE) {
                continue;
            }
            ZX_ASSERT(observed & (ZX_SOCKET_PEER_CLOSED | ZX_SOCKET_PEER_WRITE_DISABLED));
            *out_actual = 0u;
            return ZX_OK;
        }
        return status;
    }
}

static zx_status_t zxs_sendmsg_stream(const zxs_socket_t* socket,
                                      const struct msghdr* msg,
                                      size_t* out_actual) {
    size_t total = 0u;
    for (int i = 0; i < msg->msg_iovlen; i++) {
        struct iovec* iov = &msg->msg_iov[i];
        if (iov->iov_len <= 0) {
            return ZX_ERR_INVALID_ARGS;
        }
        size_t actual = 0u;
        zx_status_t status = zxs_write(socket, iov->iov_base, iov->iov_len,
                                       &actual);
        if (status != ZX_OK) {
            if (total > 0) {
                break;
            }
            return status;
        }
        total += actual;
        if (actual != iov->iov_len) {
            break;
        }
    }
    *out_actual = total;
    return ZX_OK;
}

static zx_status_t zxs_sendmsg_dgram(const zxs_socket_t* socket,
                                     const struct msghdr* msg,
                                     size_t* out_actual) {
    size_t total = 0u;
    for (int i = 0; i < msg->msg_iovlen; i++) {
        struct iovec* iov = &msg->msg_iov[i];
        if (iov->iov_len <= 0) {
            return ZX_ERR_INVALID_ARGS;
        }
        total += iov->iov_len;
    }
    size_t encoded_size = total + FDIO_SOCKET_MSG_HEADER_SIZE;

    // TODO: avoid malloc m
    fdio_socket_msg_t* m = static_cast<fdio_socket_msg_t*>(malloc(encoded_size));
    if (msg->msg_name != nullptr) {
        // TODO(abarth): Validate msg->msg_namelen against sizeof(m->addr).
        memcpy(&m->addr, msg->msg_name, msg->msg_namelen);
    }
    m->addrlen = msg->msg_namelen;
    m->flags = 0;
    char* data = m->data;
    for (int i = 0; i < msg->msg_iovlen; i++) {
        struct iovec* iov = &msg->msg_iov[i];
        memcpy(data, iov->iov_base, iov->iov_len);
        data += iov->iov_len;
    }
    size_t actual = 0u;
    zx_status_t status = zxs_write(socket, m, encoded_size, &actual);
    free(m);
    if (status == ZX_OK) {
        *out_actual = total;
    }
    return status;
}

static zx_status_t zxs_recvmsg_stream(const zxs_socket_t* socket,
                                      struct msghdr* msg,
                                      size_t* out_actual) {
    size_t total = 0u;
    for (int i = 0; i < msg->msg_iovlen; i++) {
        struct iovec* iov = &msg->msg_iov[i];
        size_t actual = 0u;
        zx_status_t status = zxs_read(socket, iov->iov_base, iov->iov_len,
                                      &actual);
        if (status != ZX_OK) {
            if (total > 0) {
                break;
            }
            return status;
        }
        total += actual;
        if (actual != iov->iov_len) {
            break;
        }
    }
    *out_actual = total;
    return ZX_OK;
}

static zx_status_t zxs_recvmsg_dgram(const zxs_socket_t* socket,
                                     struct msghdr* msg,
                                     size_t* out_actual) {
    // Read 1 extra byte to detect if the buffer is too small to fit the whole
    // packet, so we can set MSG_TRUNC flag if necessary.
    size_t encoded_size = FDIO_SOCKET_MSG_HEADER_SIZE + 1;
    for (int i = 0; i < msg->msg_iovlen; i++) {
        struct iovec* iov = &msg->msg_iov[i];
        if (iov->iov_len <= 0) {
            return ZX_ERR_INVALID_ARGS;
        }
        encoded_size += iov->iov_len;
    }

    // TODO: avoid malloc
    fdio_socket_msg_t* m = static_cast<fdio_socket_msg_t*>(malloc(encoded_size));
    size_t actual = 0u;
    zx_status_t status = zxs_read(socket, m, encoded_size, &actual);
    if (status != ZX_OK) {
        free(m);
        return status;
    }
    if (actual < FDIO_SOCKET_MSG_HEADER_SIZE) {
        free(m);
        return ZX_ERR_INTERNAL;
    }
    actual -= FDIO_SOCKET_MSG_HEADER_SIZE;
    if (msg->msg_name != nullptr) {
        int bytes_to_copy = (msg->msg_namelen < m->addrlen) ? msg->msg_namelen : m->addrlen;
        memcpy(msg->msg_name, &m->addr, bytes_to_copy);
    }
    msg->msg_namelen = m->addrlen;
    msg->msg_flags = m->flags;
    char* data = m->data;
    size_t remaining = actual;
    for (int i = 0; i < msg->msg_iovlen; i++) {
        struct iovec* iov = &msg->msg_iov[i];
        if (remaining == 0) {
            iov->iov_len = 0;
        } else {
            if (remaining < iov->iov_len)
                iov->iov_len = remaining;
            memcpy(iov->iov_base, data, iov->iov_len);
            data += iov->iov_len;
            remaining -= iov->iov_len;
        }
    }

    if (remaining > 0) {
        msg->msg_flags |= MSG_TRUNC;
        actual -= remaining;
    }

    free(m);
    *out_actual = actual;
    return ZX_OK;
}

zx_status_t zxs_send(const zxs_socket_t* socket, const void* buffer,
                     size_t capacity, size_t* out_actual) {
    if (socket->flags & ZXS_FLAG_DATAGRAM) {
        struct iovec iov;
        iov.iov_base = const_cast<void*>(buffer);
        iov.iov_len = capacity;

        struct msghdr msg;
        msg.msg_name = nullptr;
        msg.msg_namelen = 0;
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = nullptr;
        msg.msg_controllen = 0;
        msg.msg_flags = 0;

        return zxs_sendmsg_dgram(socket, &msg, out_actual);
    } else {
        return zxs_write(socket, buffer, capacity, out_actual);
    }
}

zx_status_t zxs_recv(const zxs_socket_t* socket, void* buffer,
                     size_t capacity, size_t* out_actual) {
    if (socket->flags & ZXS_FLAG_DATAGRAM) {
        struct iovec iov;
        iov.iov_base = buffer;
        iov.iov_len = capacity;

        struct msghdr msg;
        msg.msg_name = nullptr;
        msg.msg_namelen = 0;
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = nullptr;
        msg.msg_controllen = 0;
        msg.msg_flags = 0;

        return zxs_recvmsg_dgram(socket, &msg, out_actual);
    } else {
        return zxs_read(socket, buffer, capacity, out_actual);
    }
}

zx_status_t zxs_sendto(const zxs_socket_t* socket, const struct sockaddr* addr,
                       size_t addr_length, const void* buffer, size_t capacity,
                       size_t* out_actual) {
    struct iovec iov;
    iov.iov_base = const_cast<void*>(buffer);
    iov.iov_len = capacity;

    struct msghdr msg;
    msg.msg_name = const_cast<struct sockaddr*>(addr);
    msg.msg_namelen = static_cast<socklen_t>(addr_length);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = nullptr;
    msg.msg_controllen = 0;
    msg.msg_flags = 0; // this field is ignored

    return zxs_sendmsg(socket, &msg, out_actual);
}

zx_status_t zxs_recvfrom(const zxs_socket_t* socket, struct sockaddr* addr,
                        size_t addr_capacity, size_t* out_addr_actual,
                        void* buffer, size_t capacity, size_t* out_actual) {
    struct iovec iov;
    iov.iov_base = buffer;
    iov.iov_len = capacity;

    struct msghdr msg;
    msg.msg_name = addr;
    msg.msg_namelen = static_cast<socklen_t>(addr_capacity);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = nullptr;
    msg.msg_controllen = 0;
    msg.msg_flags = 0;

    zx_status_t status = zxs_recvmsg(socket, &msg, out_actual);
    *out_addr_actual = msg.msg_namelen;
    return status;
}

zx_status_t zxs_sendmsg(const zxs_socket_t* socket, const struct msghdr* msg,
                        size_t* out_actual) {
    if (socket->flags & ZXS_FLAG_DATAGRAM) {
        return zxs_sendmsg_dgram(socket, msg, out_actual);
    } else {
        return zxs_sendmsg_stream(socket, msg, out_actual);
    }
}
zx_status_t zxs_recvmsg(const zxs_socket_t* socket, struct msghdr* msg,
                        size_t* out_actual) {
    if (socket->flags & ZXS_FLAG_DATAGRAM) {
        return zxs_recvmsg_dgram(socket, msg, out_actual);
    } else {
        return zxs_recvmsg_stream(socket, msg, out_actual);
    }
}

zx_status_t zxs_ioctl(const zxs_socket_t* socket, uint32_t op,
                      const void* in_buffer, size_t in_capacity,
                      void* out_buffer, size_t out_capacity,
                      size_t* out_actual) {
    if (in_capacity > ZXSIO_PAYLOAD_SZ || out_capacity > ZXSIO_PAYLOAD_SZ) {
        return ZX_ERR_INVALID_ARGS;
    }

    if (IOCTL_KIND(op) != IOCTL_KIND_DEFAULT) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zxsio_msg_t msg;
    memset(&msg, 0, ZXSIO_HDR_SZ);
    msg.op = ZXSIO_IOCTL;
    msg.datalen = static_cast<uint32_t>(in_capacity);
    msg.arg = static_cast<uint32_t>(out_capacity);
    msg.arg2.op = op;
    memcpy(msg.data, in_buffer, in_capacity);

    zx_status_t status = zxsio_txn(socket->socket, &msg);
    if (status < 0) {
        return status;
    }

    size_t copy_length = msg.datalen;
    if (msg.datalen > out_capacity) {
        copy_length = out_capacity;
    }

    memcpy(out_buffer, msg.data, copy_length);
    *out_actual = copy_length;
    return status;
}
