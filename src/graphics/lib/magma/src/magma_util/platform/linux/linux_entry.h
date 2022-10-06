// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LINUX_ENTRY_H
#define LINUX_ENTRY_H

#include <inttypes.h>

#include "magma/magma_common_defs.h"

// Transfers ownership of |device_handle|.
typedef magma_status_t (*magma_open_device_t)(uint32_t device_handle, uint32_t table_size,
                                              void* method_table_out[], void** context_out);

typedef magma_status_t (*magma_device_query_t)(void* context, uint64_t query_id,
                                               uint64_t* result_out);

typedef magma_status_t (*magma_device_connect_t)(void* context, uint64_t client_id,
                                                 void** delegate_out);

typedef void (*magma_device_release_t)(void* context);

enum {
  kMagmaDeviceOrdinalQuery = 0,
  kMagmaDeviceOrdinalConnect = 1,
  kMagmaDeviceOrdinalRelease = 2,
  kMagmaDeviceOrdinalTableSize,
};

#endif  // LINUX_ENTRY_H
