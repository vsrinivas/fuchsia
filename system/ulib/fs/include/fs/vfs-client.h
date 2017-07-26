// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <mxio/remoteio.h>

#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>

#include <magenta/assert.h>
#include <magenta/types.h>

#include <mxio/vfs.h>

__BEGIN_CDECLS

// Send an unmount signal on a handle to a filesystem and await a response.
mx_status_t vfs_unmount_handle(mx_handle_t h, mx_time_t deadline);

__END_CDECLS
