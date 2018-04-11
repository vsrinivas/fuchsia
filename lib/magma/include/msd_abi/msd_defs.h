// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MAGMA_INCLUDE_MSD_ABI_MSD_DEFS_H_
#define GARNET_LIB_MAGMA_INCLUDE_MSD_ABI_MSD_DEFS_H_

#include "magma_common_defs.h"
#include <stdint.h>

#define MSD_DRIVER_CONFIG_TEST_NO_DEVICE_THREAD 1

#if defined(__cplusplus)
extern "C" {
#endif

typedef uint64_t msd_client_id_t;
typedef uint32_t msd_channel_t;

// |data| is vendor-specific.
typedef magma_status_t (*msd_channel_send_callback_t)(msd_channel_t channel, void* data,
                                                      uint64_t data_size);

// The magma system driver... driver :)
struct msd_driver_t {
    int32_t magic_;
};

// The magma system driver device.
struct msd_device_t {
    int32_t magic_;
};

// A driver defined connection, owned by the MagmaSystemConnection
struct msd_connection_t {
    int32_t magic_;
};

// A driver defined buffer that owns a reference to an msd_platform_buffer
struct msd_buffer_t {
    int32_t magic_;
};

// A driver defined context, owned by the magma system context
struct msd_context_t {
    int32_t magic_;
};

struct msd_semaphore_t {
    int32_t magic_;
};

#if defined(__cplusplus)
}
#endif

#endif /* _MSD_DEFS_H_ */
