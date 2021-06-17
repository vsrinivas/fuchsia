// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_dependency_injection_device.h"

#include <ddktl/fidl.h>

#include "magma_util/macros.h"

namespace magma {

MagmaDependencyInjectionDevice::MagmaDependencyInjectionDevice(zx_device_t* parent, Owner* owner)
    : DdkDependencyInjectionDeviceType(parent),
      owner_(owner),
      server_loop_(&kAsyncLoopConfigNeverAttachToThread) {}

// static
zx_status_t MagmaDependencyInjectionDevice::Bind(
    std::unique_ptr<MagmaDependencyInjectionDevice> device) {
  zx_status_t status = device->DdkAdd("gpu-dependency-injection");
  if (status == ZX_OK) {
    // DDK took ownership of the device.
    device.release();
  }
  return DRET(status);
}

void MagmaDependencyInjectionDevice::SetMemoryPressureProvider(
    SetMemoryPressureProviderRequestView request,
    SetMemoryPressureProviderCompleter::Sync& completer) {
  if (pressure_server_)
    return;

  server_loop_.StartThread("memory-pressure-loop");
  auto endpoints = fidl::CreateEndpoints<fuchsia_memorypressure::Watcher>();
  if (!endpoints.is_ok()) {
    MAGMA_LOG(WARNING, "Failed to create fidl Endpoints");
    return;
  }
  pressure_server_ = fidl::BindServer<fidl::WireServer<fuchsia_memorypressure::Watcher>>(
      server_loop_.dispatcher(), std::move(endpoints->server), this);

  auto provider = fidl::BindSyncClient(std::move(request->provider));
  provider.RegisterWatcher(std::move(endpoints->client));
}

static MagmaMemoryPressureLevel GetMagmaLevel(fuchsia_memorypressure::wire::Level level) {
  switch (level) {
    case fuchsia_memorypressure::wire::Level::kNormal:
      return MAGMA_MEMORY_PRESSURE_LEVEL_NORMAL;
    case fuchsia_memorypressure::wire::Level::kWarning:
      return MAGMA_MEMORY_PRESSURE_LEVEL_WARNING;
    case fuchsia_memorypressure::wire::Level::kCritical:
      return MAGMA_MEMORY_PRESSURE_LEVEL_CRITICAL;
    default:
      return MAGMA_MEMORY_PRESSURE_LEVEL_NORMAL;
  }
}

void MagmaDependencyInjectionDevice::OnLevelChanged(OnLevelChangedRequestView request,
                                                    OnLevelChangedCompleter::Sync& completer) {
  owner_->SetMemoryPressureLevel(GetMagmaLevel(request->level));
  completer.Reply();
}

}  // namespace magma
