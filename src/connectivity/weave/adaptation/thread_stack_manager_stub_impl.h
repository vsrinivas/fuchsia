// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WEAVE_ADAPTATION_THREAD_STACK_MANAGER_STUB_IMPL_H_
#define SRC_CONNECTIVITY_WEAVE_ADAPTATION_THREAD_STACK_MANAGER_STUB_IMPL_H_

// clang-format off
#pragma GCC diagnostic push
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#include <Weave/DeviceLayer/internal/DeviceNetworkInfo.h>
#include <Weave/DeviceLayer/ThreadStackManager.h>
#pragma GCC diagnostic pop
// clang-format on

namespace nl {
namespace Weave {
namespace DeviceLayer {

class NL_DLL_EXPORT ThreadStackManagerStubImpl : public ThreadStackManagerImpl::Delegate {
 public:
  // ThreadStackManager implementations.

  WEAVE_ERROR InitThreadStack() override { return WEAVE_NO_ERROR; }
  bool HaveRouteToAddress(const IPAddress& destAddr) override { return true; }
  void OnPlatformEvent(const WeaveDeviceEvent* event) override {}
  bool IsThreadEnabled() override { return true; }
  WEAVE_ERROR SetThreadEnabled(bool val) override { return WEAVE_NO_ERROR; }
  bool IsThreadProvisioned() override { return true; }
  bool IsThreadAttached() override { return true; }
  WEAVE_ERROR GetThreadProvision(Internal::DeviceNetworkInfo& netInfo,
                                 bool includeCredentials) override {
    return WEAVE_ERROR_INCORRECT_STATE;
  }
  WEAVE_ERROR SetThreadProvision(const Internal::DeviceNetworkInfo& netInfo) override {
    return WEAVE_NO_ERROR;
  }
  void ClearThreadProvision() override {}
  ConnectivityManager::ThreadDeviceType GetThreadDeviceType() override {
    return ConnectivityManager::ThreadDeviceType::kThreadDeviceType_Router;
  }
  bool HaveMeshConnectivity() override { return true; }
  WEAVE_ERROR GetAndLogThreadStatsCounters() override { return WEAVE_NO_ERROR; }
  WEAVE_ERROR GetAndLogThreadTopologyMinimal() override { return WEAVE_NO_ERROR; }
  WEAVE_ERROR GetAndLogThreadTopologyFull() override { return WEAVE_NO_ERROR; }
  std::string GetInterfaceName() const override { return "dummy0"; }
  bool IsThreadSupported() const override { return true; }
  WEAVE_ERROR GetPrimary802154MACAddress(uint8_t* mac_address) override;
  WEAVE_ERROR SetThreadJoinable(bool enable) override { return WEAVE_NO_ERROR; }
  nl::Weave::Profiles::DataManagement::event_id_t LogNetworkWpanStatsEvent(
      Schema::Nest::Trait::Network::TelemetryNetworkWpanTrait::NetworkWpanStatsEvent* event)
      override;

 private:
  std::unique_ptr<Internal::DeviceNetworkInfo> network_info_;
};

}  // namespace DeviceLayer
}  // namespace Weave
}  // namespace nl

#endif  // SRC_CONNECTIVITY_WEAVE_ADAPTATION_THREAD_STACK_MANAGER_STUB_IMPL_H_
