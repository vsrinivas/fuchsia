// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
#pragma GCC diagnostic push
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#include <Weave/DeviceLayer/PlatformManager.h>
#pragma GCC diagnostic pop

#include "src/connectivity/weave/adaptation/configuration_manager_delegate_impl.h"
#include "src/connectivity/weave/adaptation/connectivity_manager_delegate_impl.h"
#include "src/connectivity/weave/adaptation/network_provisioning_server_delegate_impl.h"
#include "src/connectivity/weave/adaptation/thread_stack_manager_delegate_impl.h"
#include "src/connectivity/weave/lib/core/trait_updater_delegate_impl.h"
// clang-format on

#include "src/connectivity/weave/weavestack/app.h"

using nl::Weave::DeviceLayer::ConfigurationManagerDelegateImpl;
using nl::Weave::DeviceLayer::ConfigurationMgrImpl;
using nl::Weave::DeviceLayer::ConnectivityManagerDelegateImpl;
using nl::Weave::DeviceLayer::ConnectivityMgrImpl;
using nl::Weave::DeviceLayer::ThreadStackManagerDelegateImpl;
using nl::Weave::DeviceLayer::ThreadStackMgrImpl;
using nl::Weave::DeviceLayer::TraitUpdater;
using nl::Weave::DeviceLayer::TraitUpdaterDelegateImpl;
using nl::Weave::DeviceLayer::Internal::NetworkProvisioningServerDelegateImpl;
using nl::Weave::DeviceLayer::Internal::NetworkProvisioningSvrImpl;

int main() {
  weavestack::App app;
  zx_status_t status;

  ConfigurationMgrImpl().SetDelegate(std::make_unique<ConfigurationManagerDelegateImpl>());
  ConnectivityMgrImpl().SetDelegate(std::make_unique<ConnectivityManagerDelegateImpl>());
  NetworkProvisioningSvrImpl().SetDelegate(
      std::make_unique<NetworkProvisioningServerDelegateImpl>());
  ThreadStackMgrImpl().SetDelegate(std::make_unique<ThreadStackManagerDelegateImpl>());
  TraitUpdater().SetDelegate(std::make_unique<TraitUpdaterDelegateImpl>());

  status = app.Init();
  if (status != ZX_OK) {
    return status;
  }

  status = app.Run();
  if (status != ZX_OK) {
    return status;
  }

  return ZX_OK;
}
