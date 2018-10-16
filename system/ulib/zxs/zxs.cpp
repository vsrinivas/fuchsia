// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zxs/zxs.h>

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
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t zxs_getpeername(const zxs_socket_t* socket, struct sockaddr* addr,
                            size_t capacity, size_t* out_actual) {
    return ZX_ERR_NOT_SUPPORTED;
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
