// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/gpu/magma/c/fidl.h>
#include <lib/zx/channel.h>

#include "platform_connection_client.h"
#include "platform_device_client.h"

namespace magma {
class ZirconPlatformDeviceClient : public PlatformDeviceClient {
 public:
  ZirconPlatformDeviceClient(magma_handle_t handle) : channel_(handle) {}

  std::unique_ptr<PlatformConnectionClient> Connect() {
    uint32_t device_handle;
    uint32_t device_notification_handle;
    zx_status_t status =
        fuchsia_gpu_magma_DeviceConnect(channel_.get(), magma::PlatformThreadId().id(),
                                        &device_handle, &device_notification_handle);
    if (status != ZX_OK)
      return DRETP(nullptr, "magma_DeviceConnect failed: %d", status);
    return magma::PlatformConnectionClient::Create(device_handle, device_notification_handle);
  }

  bool Query(uint64_t query_id, uint64_t* result_out) {
    zx_status_t status = fuchsia_gpu_magma_DeviceQuery(channel_.get(), query_id, result_out);

    if (status != ZX_OK)
      return DRETF(false, "magma_DeviceQuery failed: %d", status);

    return true;
  }

  bool QueryReturnsBuffer(uint64_t query_id, magma_handle_t* buffer_out) {
    *buffer_out = ZX_HANDLE_INVALID;
    zx_status_t status =
        fuchsia_gpu_magma_DeviceQueryReturnsBuffer(channel_.get(), query_id, buffer_out);
    if (status != ZX_OK)
      return DRETF(false, "magma_DeviceQueryReturnsBuffer failed: %d", status);

    return true;
  }

 private:
  zx::channel channel_;
};

// static
std::unique_ptr<PlatformDeviceClient> PlatformDeviceClient::Create(uint32_t handle) {
  return std::make_unique<ZirconPlatformDeviceClient>(handle);
}
}  // namespace magma
