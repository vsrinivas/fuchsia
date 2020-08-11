// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_MAGMA_INCLUDE_MSD_ABI_MSD_DEFS_H_
#define SRC_GRAPHICS_LIB_MAGMA_INCLUDE_MSD_ABI_MSD_DEFS_H_

#include <stdint.h>

#include "magma_common_defs.h"

#define MSD_DRIVER_CONFIG_TEST_NO_DEVICE_THREAD 1
#define MSD_CHANNEL_SEND_MAX_SIZE 64

#if defined(__cplusplus)
extern "C" {
#endif

typedef uint64_t msd_client_id_t;

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
struct msd_perf_count_pool {
  int32_t magic_;
};

enum MSD_CONNECTION_NOTIFICATION_TYPE {
  MSD_CONNECTION_NOTIFICATION_CHANNEL_SEND = 1,
  MSD_CONNECTION_NOTIFICATION_CONTEXT_KILLED = 2,
  MSD_CONNECTION_NOTIFICATION_PERFORMANCE_COUNTERS_READ_COMPLETED = 3,
};

struct msd_notification_t {
  uint32_t type;
  union {
    struct {
      uint8_t data[MSD_CHANNEL_SEND_MAX_SIZE];
      uint32_t size;
    } channel_send;
    struct {
      uint64_t pool_id;
      uint32_t trigger_id;
      uint64_t buffer_id;
      uint32_t buffer_offset;
      uint64_t timestamp;
      uint32_t result_flags;
    } perf_counter_result;
  } u;
};

typedef void (*msd_connection_notification_callback_t)(void* token,
                                                       struct msd_notification_t* notification);

#if defined(__cplusplus)
}
#endif

#endif /* _MSD_DEFS_H_ */
