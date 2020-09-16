// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_CONNECTIVITY_WEAVE_ADAPTATION_CONNECTIVITY_MANAGER_DELEGATE_IMPL_H_
#define SRC_CONNECTIVITY_WEAVE_ADAPTATION_CONNECTIVITY_MANAGER_DELEGATE_IMPL_H_

#include <Weave/DeviceLayer/ConnectivityManager.h>

namespace nl::Weave::DeviceLayer {

/**
 * A concrete implementation of the delegate used by ConnectivityManagerImpl to
 * make the required platform calls needed to manage connectivity state in Weave.
 */
class ConnectivityManagerDelegateImpl : public ConnectivityManagerImpl::Delegate {
 public:
  // ConnectivityManagerImpl::Delegate APIs
  WEAVE_ERROR Init() override;
  bool IsServiceTunnelConnected() override;
  bool IsServiceTunnelRestricted() override;
  void OnPlatformEvent(const WeaveDeviceEvent* event) override;

 private:
  // Initializes the service tunnel agent in OpenWeave. This function primarily
  // exists to allow unittests to override its behavior.
  virtual WEAVE_ERROR InitServiceTunnelAgent();

  // Handle service tunnel notifications, where |reason| specifies the
  // reason type for this event, |err| indicates errors during the TUN
  // operation, and |app_ctx| holds any application-specific context.
  static void HandleServiceTunnelNotification(
      nl::Weave::Profiles::WeaveTunnel::WeaveTunnelConnectionMgr::TunnelConnNotifyReasons reason,
      WEAVE_ERROR err, void* app_ctx);

  // Start the service tunnel.
  void StartServiceTunnel();
  // Stop the service tunnel.
  void StopServiceTunnel();
  // Stop the service tunnel with the given error code.
  void StopServiceTunnel(WEAVE_ERROR err);
  // Returns whether the tunnel should be started.
  bool ShouldStartServiceTunnel();
};

}  // namespace nl::Weave::DeviceLayer

#endif  // SRC_CONNECTIVITY_WEAVE_ADAPTATION_CONNECTIVITY_MANAGER_DELEGATE_IMPL_H_
