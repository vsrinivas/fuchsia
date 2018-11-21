// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/zxio/inception.h>

#include "private.h"

zxio_remote_t* fdio_get_zxio_remote(fdio_t* io);

// open operation directly on remoteio handle
zx_status_t zxrio_open_handle(zx_handle_t h, const char* path, uint32_t flags,
                              uint32_t mode, fdio_t** out);

extern fdio_ops_t fdio_zxio_remote_ops;
