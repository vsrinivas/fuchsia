// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FDIO_PRIVATE_SOCKET_H_
#define LIB_FDIO_PRIVATE_SOCKET_H_

#include "internal.h"

// SIO signals
#define ZXSIO_SIGNAL_INCOMING ZX_USER_SIGNAL_0
#define ZXSIO_SIGNAL_OUTGOING ZX_USER_SIGNAL_1
#define ZXSIO_SIGNAL_CONNECTED ZX_USER_SIGNAL_3
#define ZXSIO_SIGNAL_SHUTDOWN_READ ZX_USER_SIGNAL_4
#define ZXSIO_SIGNAL_SHUTDOWN_WRITE ZX_USER_SIGNAL_5

bool fdio_is_socket(fdio_t* io);

#endif  // LIB_FDIO_PRIVATE_SOCKET_H_
