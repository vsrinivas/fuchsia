// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_MAGMA_INCLUDE_MSD_MSD_DEFS_H_
#define SRC_GRAPHICS_LIB_MAGMA_INCLUDE_MSD_MSD_DEFS_H_

#include <stdint.h>

#include "magma/magma_common_defs.h"

#define MSD_DRIVER_CONFIG_TEST_NO_DEVICE_THREAD 1
// Designed so that msd_notification_t fits in a page
#define MSD_CHANNEL_SEND_MAX_SIZE (4096 - sizeof(uint64_t) - sizeof(uint32_t))

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
  MSD_CONNECTION_NOTIFICATION_HANDLE_WAIT = 4,
  MSD_CONNECTION_NOTIFICATION_HANDLE_WAIT_CANCEL = 5,
};

typedef void (*msd_connection_handle_wait_complete_t)(void* context, magma_status_t status,
                                                      magma_handle_t handle);
typedef void (*msd_connection_handle_wait_start_t)(void* context, void* cancel_token);

// TODO(fxbug.dev/100946) - rename to "callback"
struct msd_notification_t {
  uint64_t type;
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
    struct {
      msd_connection_handle_wait_start_t starter;
      msd_connection_handle_wait_complete_t completer;
      void* wait_context;
      magma_handle_t handle;
    } handle_wait;
    struct {
      void* cancel_token;
    } handle_wait_cancel;
  } u;
};

enum IcdSupportFlags {
  ICD_SUPPORT_FLAG_VULKAN = 1,
  ICD_SUPPORT_FLAG_OPENCL = 2,
  ICD_SUPPORT_FLAG_MEDIA_CODEC_FACTORY = 4,
};

typedef struct msd_icd_info_t {
  // Same length as fuchsia.url.MAX_URL_LENGTH.
  char component_url[4096];
  uint32_t support_flags;
} msd_icd_info_t;

typedef void (*msd_connection_notification_callback_t)(void* token,
                                                       struct msd_notification_t* notification);

enum MagmaMemoryPressureLevel {
  MAGMA_MEMORY_PRESSURE_LEVEL_NORMAL = 1,
  MAGMA_MEMORY_PRESSURE_LEVEL_WARNING = 2,
  MAGMA_MEMORY_PRESSURE_LEVEL_CRITICAL = 3,
};

#if defined(__cplusplus)
}
#endif

#endif /* _MSD_DEFS_H_ */
