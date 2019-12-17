// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform_connection_client.h"
#include "platform_device_client.h"

namespace magma {

class LinuxPlatformDeviceClient : public PlatformDeviceClient {
 public:
  LinuxPlatformDeviceClient(uint32_t handle) : handle_(handle) {}

  std::unique_ptr<PlatformConnectionClient> Connect() {
    return magma::PlatformConnectionClient::Create(handle_, 0);
  }

  bool Query(uint64_t query_id, uint64_t* result_out) {
    return DRETF(false, "LinuxPlatformDeviceClient::Query not implemented");
  }

  bool QueryReturnsBuffer(uint64_t query_id, magma_handle_t* buffer_out) {
    return DRETF(false, "LinuxPlatformDeviceClient::QueryReturnsBuffer not implemented");
  }

 private:
  uint32_t handle_;
};

// static
std::unique_ptr<PlatformDeviceClient> PlatformDeviceClient::Create(uint32_t handle) {
  return std::make_unique<LinuxPlatformDeviceClient>(handle);
}

}  // namespace magma
