// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fdio/remoteio.h>

#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>

#include <zircon/assert.h>
#include <zircon/types.h>

#include <fdio/vfs.h>

__BEGIN_CDECLS

// Send an unmount signal on a handle to a filesystem and await a
// response. Unconditionally consumes |h|.
zx_status_t vfs_unmount_handle(zx_handle_t h, zx_time_t deadline);

__END_CDECLS
