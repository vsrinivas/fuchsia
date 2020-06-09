// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_MAGMA_SRC_MAGMA_UTIL_PLATFORM_ZIRCON_MAGMA_PERFORMANCE_COUNTER_DEVICE_H_
#define SRC_GRAPHICS_LIB_MAGMA_SRC_MAGMA_UTIL_PLATFORM_ZIRCON_MAGMA_PERFORMANCE_COUNTER_DEVICE_H_

#include <fuchsia/gpu/magma/llcpp/fidl.h>
#include <lib/zx/event.h>

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>

namespace magma {

class MagmaPerformanceCounterDevice;
using DdkPerfCountDeviceType = ddk::Device<MagmaPerformanceCounterDevice, ddk::Messageable>;

class MagmaPerformanceCounterDevice
    : public llcpp::fuchsia::gpu::magma::PerformanceCounterAccess::Interface,
      public DdkPerfCountDeviceType,
      public ddk::EmptyProtocol<ZX_PROTOCOL_GPU_PERFORMANCE_COUNTERS> {
 public:
  explicit MagmaPerformanceCounterDevice(zx_device_t* parent);

  // Parent should be the GPU device itself. That way this device is released when the parent device
  // is released. Returns the koid of the event that was created.
  static bool AddDevice(zx_device_t* parent, zx_koid_t* koid_out);

  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);
  void DdkRelease() { delete this; }

 private:
  // Takes ownership of |*device| on success.
  zx_status_t Bind(std::unique_ptr<MagmaPerformanceCounterDevice>* device);

  zx_koid_t GetEventKoid();

  void GetPerformanceCountToken(GetPerformanceCountTokenCompleter::Sync completer) override;

  // This is the access token that will be given to PerformanceCounterAccess clients, and which the
  // MSD will compare against to validate access permissions.
  zx::event event_;
};

}  // namespace magma

#endif  // SRC_GRAPHICS_LIB_MAGMA_SRC_MAGMA_UTIL_PLATFORM_ZIRCON_MAGMA_PERFORMANCE_COUNTER_DEVICE_H_
