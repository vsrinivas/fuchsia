// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _MSD_DEFS_H_
#define _MSD_DEFS_H_

#include "magma_system_common_defs.h"
#include <stdint.h>

#define MSD_DRIVER_CONFIG_TEST_NO_DEVICE_THREAD 1

#if defined(__cplusplus)
extern "C" {
#endif

typedef uint32_t msd_client_id;

// The magma system driver... driver :)
struct msd_driver {
    int32_t magic_;
};

// The magma system driver device.
struct msd_device {
    int32_t magic_;
};

// A driver defined connection, owned by the MagmaSystemConnection
struct msd_connection {
    int32_t magic_;
};

// A driver defined buffer that owns a reference to an msd_platform_buffer
struct msd_buffer {
    int32_t magic_;
};

// A driver defined context, owned by the magma system context
struct msd_context {
    int32_t magic_;
};

struct msd_semaphore {
    int32_t magic_;
};

#if defined(__cplusplus)
}
#endif

#endif /* _MSD_DEFS_H_ */
