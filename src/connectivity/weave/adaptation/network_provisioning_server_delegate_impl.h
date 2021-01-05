// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WEAVE_ADAPTATION_NETWORK_PROVISIONING_SERVER_DELEGATE_IMPL_H_
#define SRC_CONNECTIVITY_WEAVE_ADAPTATION_NETWORK_PROVISIONING_SERVER_DELEGATE_IMPL_H_

// clang-format off
#include <Weave/DeviceLayer/internal/NetworkProvisioningServer.h>
// clang-format on

#include <fuchsia/weave/cpp/fidl.h>

namespace nl::Weave::DeviceLayer::Internal {

/**
 * A concrete implementation of the delegate used by NetworkProvisioningServerImpl to
 * make the required platform calls needed to manage connectivity state in Weave.
 */
class NL_DLL_EXPORT NetworkProvisioningServerDelegateImpl
    : public NetworkProvisioningServerImpl::Delegate {
 public:
  // NetworkProvisioningServerImpl::Delegate APIs
  WEAVE_ERROR Init();
  WEAVE_ERROR GetWiFiStationProvision(DeviceNetworkInfo& net_info, bool include_credentials);

  // Set WLAN Network Config Provider which can be used to watch WLAN network provision updates.
  void SetWlanNetworkConfigProvider(
      fidl::InterfaceHandle<class fuchsia::weave::WlanNetworkConfigProvider> provider);
  // Callback function for WLAN network provision updates.
  void OnWlanNetworkUpdate(fuchsia::wlan::policy::NetworkConfig current_network_config);

 private:
  fuchsia::weave::WlanNetworkConfigProviderPtr wlan_network_config_provider_;
  fuchsia::wlan::policy::NetworkConfig current_network_config_;

  static nl::Weave::Profiles::NetworkProvisioning::WiFiSecurityType ConvertToWiFiSecurityType(
      fuchsia::wlan::policy::SecurityType type);
};

}  // namespace nl::Weave::DeviceLayer::Internal

#endif  // SRC_CONNECTIVITY_WEAVE_ADAPTATION_NETWORK_PROVISIONING_SERVER_DELEGATE_IMPL_H_
