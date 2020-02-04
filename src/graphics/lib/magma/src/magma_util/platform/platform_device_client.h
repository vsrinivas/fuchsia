// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_MAGMA_SRC_MAGMA_UTIL_PLATFORM_PLATFORM_DEVICE_CLIENT_H_
#define SRC_GRAPHICS_LIB_MAGMA_SRC_MAGMA_UTIL_PLATFORM_PLATFORM_DEVICE_CLIENT_H_

#include "platform_connection_client.h"

namespace magma {
class PlatformDeviceClient {
 public:
  virtual ~PlatformDeviceClient() = default;

  static std::unique_ptr<PlatformDeviceClient> Create(magma_handle_t handle);

  virtual std::unique_ptr<PlatformConnectionClient> Connect() = 0;

  virtual bool Query(uint64_t query_id, uint64_t* result_out) = 0;
  virtual bool QueryReturnsBuffer(uint64_t query_id, magma_handle_t* buffer_out) = 0;
};

}  // namespace magma

#endif  // SRC_GRAPHICS_LIB_MAGMA_SRC_MAGMA_UTIL_PLATFORM_PLATFORM_DEVICE_CLIENT_H_
