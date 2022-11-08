// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_CONNECTIVITY_WEAVE_ADAPTATION_CONNECTIVITY_MANAGER_DELEGATE_IMPL_H_
#define SRC_CONNECTIVITY_WEAVE_ADAPTATION_CONNECTIVITY_MANAGER_DELEGATE_IMPL_H_

#include <fuchsia/net/interfaces/cpp/fidl.h>

#include <unordered_set>

#pragma GCC diagnostic push
#include <Weave/DeviceLayer/ConnectivityManager.h>
#pragma GCC diagnostic pop

namespace nl::Weave::DeviceLayer {

/**
 * A concrete implementation of the delegate used by ConnectivityManagerImpl to
 * make the required platform calls needed to manage connectivity state in Weave.
 */
class NL_DLL_EXPORT ConnectivityManagerDelegateImpl : public ConnectivityManagerImpl::Delegate {
 public:
  // ConnectivityManagerImpl::Delegate APIs
  WEAVE_ERROR Init() override;
  bool IsServiceTunnelConnected() override;
  bool IsServiceTunnelRestricted() override;
  void OnPlatformEvent(const WeaveDeviceEvent* event) override;
  std::optional<std::string> GetWiFiInterfaceName() override;
  ConnectivityManager::ThreadMode GetThreadMode() override;
  fuchsia::net::interfaces::admin::ControlSyncPtr* GetTunInterfaceControlSyncPtr() override;

 private:
  // Initializes the service tunnel agent in OpenWeave. This function primarily
  // exists to allow unittests to override its behavior.
  virtual WEAVE_ERROR InitServiceTunnelAgent();

  // Handle events from fuchsia.net.interfaces to maintain connectivity state.
  void OnInterfaceEvent(fuchsia::net::interfaces::Event event);

  // Handle service tunnel notifications, where |reason| specifies the
  // reason type for this event, |err| indicates errors during the TUN
  // operation, and |app_ctx| holds any application-specific context.
  static void HandleServiceTunnelNotification(
      nl::Weave::Profiles::WeaveTunnel::WeaveTunnelConnectionMgr::TunnelConnNotifyReasons reason,
      WEAVE_ERROR err, void* app_ctx);

  // Start the service tunnel.
  void StartServiceTunnel();
  // Stop the service tunnel with the given error code.
  void StopServiceTunnel(WEAVE_ERROR err = WEAVE_NO_ERROR);
  // Drives service tunnel state.
  void DriveServiceTunnelState();

  // Refresh the TCP/UDP Endpoints based on network state.
  virtual WEAVE_ERROR RefreshEndpoints();

  fuchsia::net::interfaces::StatePtr state_;
  fuchsia::net::interfaces::WatcherPtr watcher_;

  uint64_t wlan_interface_id_ = 0;
  std::optional<std::string> wlan_interface_name_ = std::nullopt;
  uint64_t thread_interface_id_ = 0;

  std::unordered_set<int> routable_v4_interfaces;
  std::unordered_set<int> routable_v6_interfaces;
};

}  // namespace nl::Weave::DeviceLayer

#endif  // SRC_CONNECTIVITY_WEAVE_ADAPTATION_CONNECTIVITY_MANAGER_DELEGATE_IMPL_H_
