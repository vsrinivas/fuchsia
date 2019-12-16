// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_FDIO_PRIVATE_SOCKET_H_
#define ZIRCON_SYSTEM_ULIB_FDIO_PRIVATE_SOCKET_H_

#include <lib/zxio/inception.h>

#include "private.h"

// SIO signals
#define ZXSIO_SIGNAL_INCOMING ZX_USER_SIGNAL_0
#define ZXSIO_SIGNAL_OUTGOING ZX_USER_SIGNAL_1
#define ZXSIO_SIGNAL_CONNECTED ZX_USER_SIGNAL_3

bool fdio_is_socket(fdio_t* io);

#endif  // ZIRCON_SYSTEM_ULIB_FDIO_PRIVATE_SOCKET_H_
