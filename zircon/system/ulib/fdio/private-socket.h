// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_FDIO_PRIVATE_SOCKET_H_
#define ZIRCON_SYSTEM_ULIB_FDIO_PRIVATE_SOCKET_H_

#include <lib/zxs/zxs.h>
#include <zircon/compiler.h>

#include "private.h"

// SIO signals
#define ZXSIO_SIGNAL_INCOMING ZX_USER_SIGNAL_0
#define ZXSIO_SIGNAL_OUTGOING ZX_USER_SIGNAL_1
// TODO(tamird): ERROR seems to be unused.
#define ZXSIO_SIGNAL_ERROR ZX_USER_SIGNAL_2
#define ZXSIO_SIGNAL_CONNECTED ZX_USER_SIGNAL_3

__BEGIN_CDECLS

// Returns a pointer to the |zxs_socket_t| inside the given |fd|, if such a
// struct exists.
//
// Caller receives a reference to the |fdio_t|. The caller is responsible for
// calling fdio_release to balance the reference count.
//
// Returns |NULL| if no |zxs_socket_t| was found.
fdio_t* fd_to_socket(int fd, const zxs_socket_t** out_socket);

__END_CDECLS

#endif // ZIRCON_SYSTEM_ULIB_FDIO_PRIVATE_SOCKET_H_
