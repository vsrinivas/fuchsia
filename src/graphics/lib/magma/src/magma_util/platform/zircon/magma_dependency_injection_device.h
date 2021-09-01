// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_MAGMA_SRC_MAGMA_UTIL_PLATFORM_ZIRCON_MAGMA_DEPENDENCY_INJECTION_DEVICE_H_
#define SRC_GRAPHICS_LIB_MAGMA_SRC_MAGMA_UTIL_PLATFORM_ZIRCON_MAGMA_DEPENDENCY_INJECTION_DEVICE_H_

#include <fidl/fuchsia.gpu.magma/cpp/wire.h>
#include <fidl/fuchsia.memorypressure/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>

#include "magma_util/macros.h"
#include "src/graphics/lib/magma/include/msd_abi/msd_defs.h"

namespace magma {

class MagmaDependencyInjectionDevice;
using DdkDependencyInjectionDeviceType =
    ddk::Device<MagmaDependencyInjectionDevice,
                ddk::Messageable<fuchsia_gpu_magma::DependencyInjection>::Mixin>;

class MagmaDependencyInjectionDevice
    : public fidl::WireServer<fuchsia_memorypressure::Watcher>,
      public DdkDependencyInjectionDeviceType,
      public ddk::EmptyProtocol<ZX_PROTOCOL_GPU_DEPENDENCY_INJECTION> {
 public:
  class Owner {
   public:
    virtual void SetMemoryPressureLevel(MagmaMemoryPressureLevel level) = 0;
  };
  // Parent should be the GPU device itself. That way this device is released before the parent
  // device is released.
  explicit MagmaDependencyInjectionDevice(zx_device_t* parent, Owner* owner);

  // Does DdkAdd on the device().
  static zx_status_t Bind(std::unique_ptr<MagmaDependencyInjectionDevice> device);

  void DdkRelease() { delete this; }

 private:
  // fuchsia:gpu::magma::DependencyInjection implementation.
  void SetMemoryPressureProvider(SetMemoryPressureProviderRequestView request,
                                 SetMemoryPressureProviderCompleter::Sync& completer) override;

  // fuchsia::memorypressure::Watcher implementation.
  void OnLevelChanged(OnLevelChangedRequestView request,
                      OnLevelChangedCompleter::Sync& completer) override;

  Owner* owner_;
  async::Loop server_loop_;
  std::optional<fidl::ServerBindingRef<fuchsia_memorypressure::Watcher>> pressure_server_;
};

}  // namespace magma

#endif  // SRC_GRAPHICS_LIB_MAGMA_SRC_MAGMA_UTIL_PLATFORM_ZIRCON_MAGMA_DEPENDENCY_INJECTION_DEVICE_H_
