// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/zxs/inception.h>

#include "private.h"

// Flags for zxsio.flags

// Set if listen() was called for this socket.
#define ZXSIO_DID_LISTEN (1<<0)

typedef struct zxsio zxsio_t;

struct zxsio {
    // base fdio io object
    fdio_t io;

    // socket handle
    zx_handle_t s;

    // see ZXSIO flags above
    uint32_t flags;
};
