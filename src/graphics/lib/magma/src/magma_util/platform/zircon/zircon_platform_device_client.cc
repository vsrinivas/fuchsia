// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.gpu.magma/cpp/wire.h>
#include <lib/zx/channel.h>

#include "magma_util/dlog.h"
#include "platform_connection_client.h"
#include "platform_device_client.h"

namespace magma {
class ZirconPlatformDeviceClient : public PlatformDeviceClient {
 public:
  ZirconPlatformDeviceClient(magma_handle_t handle) : channel_(handle) {}

  std::unique_ptr<PlatformConnectionClient> Connect() {
    uint64_t inflight_params = 0;

    if (!Query(MAGMA_QUERY_MAXIMUM_INFLIGHT_PARAMS, &inflight_params))
      return DRETP(nullptr, "Query(MAGMA_QUERY_MAXIMUM_INFLIGHT_PARAMS) failed");

    uint32_t device_handle;
    uint32_t device_notification_handle;
    auto result = fidl::WireCall<fuchsia_gpu_magma::Device>(channel_.borrow())
                      ->Connect(magma::PlatformThreadId().id());
    if (result.status() != ZX_OK)
      return DRETP(nullptr, "magma_DeviceConnect failed: %d", result.status());

    device_handle = result->primary_channel.release();
    device_notification_handle = result->notification_channel.release();

    uint64_t max_inflight_messages = magma::upper_32_bits(inflight_params);
    uint64_t max_inflight_bytes = magma::lower_32_bits(inflight_params) * 1024 * 1024;

    return magma::PlatformConnectionClient::Create(device_handle, device_notification_handle,
                                                   max_inflight_messages, max_inflight_bytes);
  }

  bool Query(uint64_t query_id, uint64_t* result_out) {
    auto result = fidl::WireCall<fuchsia_gpu_magma::Device>(channel_.borrow())->Query2(query_id);

    if (result.status() != ZX_OK)
      return DRETF(false, "magma_DeviceQuery failed: %d", result.status());
    if (result->result.is_err())
      return DRETF(false, "Got error response: %d", result->result.err());

    *result_out = result->result.response().result;
    return true;
  }

  bool QueryReturnsBuffer(uint64_t query_id, magma_handle_t* buffer_out) {
    *buffer_out = ZX_HANDLE_INVALID;
    auto result =
        fidl::WireCall<fuchsia_gpu_magma::Device>(channel_.borrow())->QueryReturnsBuffer(query_id);
    if (result.status() != ZX_OK)
      return DRETF(false, "magma_DeviceQueryReturnsBuffer failed: %d", result.status());
    if (result->result.is_err())
      return DRETF(false, "Got error response: %d", result->result.err());

    zx::vmo buffer = std::move(result->result.mutable_response().buffer);
    *buffer_out = buffer.release();

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
