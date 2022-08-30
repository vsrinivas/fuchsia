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

__END_CDECLS

#endif  // LIB_ZXIO_INCLUDE_LIB_ZXIO_BSDSOCKET_H_
