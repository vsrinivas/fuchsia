// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZXIO_INCLUDE_LIB_ZXIO_BSDSOCKET_H_
#define LIB_ZXIO_INCLUDE_LIB_ZXIO_BSDSOCKET_H_

#include <lib/zxio/types.h>
#include <lib/zxio/zxio.h>
#include <sys/socket.h>
#include <zircon/types.h>

__BEGIN_CDECLS

#define ZXIO_EXPORT __EXPORT

typedef zx_status_t (*zxio_service_connector)(const char* service_name,
                                              zx_handle_t* provider_handle);

// Creates a socket. Expects |service_connector| to yield a borrowed handle to the respective
// socket provider service. |allocator| is expected to allocate storage for a zxio_t object.
// On success, |*out_context| will point to the object allocated by |allocator|.
ZXIO_EXPORT zx_status_t zxio_socket(zxio_service_connector service_connector, int domain, int type,
                                    int protocol, zxio_storage_alloc allocator, void** out_context,
                                    int16_t* out_code);

// Binds the socket referred to in |io| to the address specified by |addr|.
ZXIO_EXPORT zx_status_t zxio_bind(zxio_t* io, const struct sockaddr* addr, socklen_t addrlen,
                                  int16_t* out_code);

// Connects the socket referred to in |io| to the address specified by |addr|.
ZXIO_EXPORT zx_status_t zxio_connect(zxio_t* io, const struct sockaddr* addr, socklen_t addrlen,
                                     int16_t* out_code);

// Marks the socket referred to in |io| as listening.
ZXIO_EXPORT zx_status_t zxio_listen(zxio_t* io, int backlog, int16_t* out_code);

// Accepts the first pending connection request on the socket referred to in |io|.
// Writes up to |*addrlen| bytes of the remote peer's address to |*addr| and sets |*addrlen|
// to the size of the remote peer's address. |*out_storage| will contain a new, connected socket.
ZXIO_EXPORT zx_status_t zxio_accept(zxio_t* io, struct sockaddr* addr, socklen_t* addrlen,
                                    zxio_storage_t* out_storage, int16_t* out_code);

// Writes up to |*addrlen| bytes of the socket's address to |*addr| and sets |*addrlen|
// to the size of the socket's address.
ZXIO_EXPORT zx_status_t zxio_getsockname(zxio_t* io, struct sockaddr* addr, socklen_t* addrlen,
                                         int16_t* out_code);

// Writes up to |*addrlen| bytes of the remote peer's address to |*addr| and sets |*addrlen|
// to the size of the remote peer's address
ZXIO_EXPORT zx_status_t zxio_getpeername(zxio_t* io, struct sockaddr* addr, socklen_t* addrlen,
                                         int16_t* out_code);

// Writes up to |*optlen| bytes of the value of the socket option specified by |level| and
// |optname| to |*optval| and sets |*optlen| to the size of the socket option.
ZXIO_EXPORT zx_status_t zxio_getsockopt(zxio_t* io, int level, int optname, void* optval,
                                        socklen_t* optlen, int16_t* out_code);

// Reads up to |optlen| bytes from |*optval| into the value of the socket option specified
// by |level| and |optname|.
ZXIO_EXPORT zx_status_t zxio_setsockopt(zxio_t* io, int level, int optname, const void* optval,
                                        socklen_t optlen, int16_t* out_code);

// Receives a message from a socket and sets |*out_actual| to the total bytes received.
ZXIO_EXPORT zx_status_t zxio_recvmsg(zxio_t* io, struct msghdr* msg, int flags, size_t* out_actual,
                                     int16_t* out_code);

// Sends a message from a socket and sets |*out_actual| to the total bytes sent.
ZXIO_EXPORT zx_status_t zxio_sendmsg(zxio_t* io, const struct msghdr* msg, int flags,
                                     size_t* out_actual, int16_t* out_code);

__END_CDECLS

#endif  // LIB_ZXIO_INCLUDE_LIB_ZXIO_BSDSOCKET_H_
