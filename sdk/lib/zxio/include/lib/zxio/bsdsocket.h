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
// |addrlen| should be initialized to the size of |*addr|.
// On success:
//   |*out_storage| holds a new, connected socket.
//   |*addr| holds the address of the peer socket.
//   |*addrlen| holds the untruncated size of the address of the peer socket.
ZXIO_EXPORT zx_status_t zxio_accept(zxio_t* io, struct sockaddr* addr, socklen_t* addrlen,
                                    zxio_storage_t* out_storage, int16_t* out_code);

// Sets |*addr| to the address to which the socket referred to in |io| is bound and |*addrlen|
// to the untruncated size of the socket address.
// |addrlen| should be initialized to the size of |*addr|.
ZXIO_EXPORT zx_status_t zxio_getsockname(zxio_t* io, struct sockaddr* addr, socklen_t* addrlen,
                                         int16_t* out_code);

// Sets |*addr| to the address of the peer of the socket referred to in |io| and |*addrlen|
// to the untruncated size of the socket address of the peer socket.
// |addrlen| should be initialized to the size of |*addr|.
ZXIO_EXPORT zx_status_t zxio_getpeername(zxio_t* io, struct sockaddr* addr, socklen_t* addrlen,
                                         int16_t* out_code);

__END_CDECLS

#endif  // LIB_ZXIO_INCLUDE_LIB_ZXIO_BSDSOCKET_H_
