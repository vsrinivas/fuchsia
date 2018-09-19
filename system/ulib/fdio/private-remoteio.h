// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "private.h"

typedef struct zxrio zxrio_t;
struct zxrio {
    // base fdio io object
    fdio_t io;

    // channel handle for rpc
    zx_handle_t h;

    // event handle for device state signals
    zx_handle_t event;
};

// These are for the benefit of namespace.c
// which needs lower level access to remoteio internals

// open operation directly on remoteio handle
zx_status_t zxrio_open_handle(zx_handle_t h, const char* path, uint32_t flags,
                              uint32_t mode, fdio_t** out);

extern fdio_ops_t zx_remote_ops;
