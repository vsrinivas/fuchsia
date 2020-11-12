// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#include <Weave/DeviceLayer/internal/DeviceNetworkInfo.h>
#include <Weave/DeviceLayer/internal/NetworkProvisioningServer.h>
#include <Weave/Core/WeaveTLV.h>
#include <Weave/Profiles/WeaveProfiles.h>
#include <Weave/Profiles/common/CommonProfile.h>

#include <Weave/DeviceLayer/internal/GenericNetworkProvisioningServerImpl.ipp>
// clang-format on

#include "network_provisioning_server_delegate_impl.h"

#include <lib/syslog/cpp/macros.h>

using namespace ::nl;
using namespace ::nl::Weave;
using namespace ::nl::Weave::Profiles::Common;
using namespace ::nl::Weave::Profiles::NetworkProvisioning;
using namespace ::nl::Weave::TLV;

using Profiles::kWeaveProfile_Common;

namespace nl {
namespace Weave {
namespace DeviceLayer {
namespace Internal {

WEAVE_ERROR NetworkProvisioningServerDelegateImpl::Init() { return WEAVE_NO_ERROR; }

void NetworkProvisioningServerDelegateImpl::OnWlanNetworkUpdate(
    ::fuchsia::wlan::policy::NetworkConfig current_network_config) {
  FX_DCHECK(wlan_network_config_provider_);
  current_network_config.Clone(&current_network_config_);
  wlan_network_config_provider_->WatchConnectedNetwork(
      fit::bind_member(this, &NetworkProvisioningServerDelegateImpl::OnWlanNetworkUpdate));
}

void NetworkProvisioningServerDelegateImpl::SetWlanNetworkConfigProvider(
    ::fidl::InterfaceHandle<class ::fuchsia::weave::WlanNetworkConfigProvider> provider) {
  // Close the channel to active provider
  if (wlan_network_config_provider_) {
    wlan_network_config_provider_.Unbind();
  }
  wlan_network_config_provider_ = provider.Bind();
  wlan_network_config_provider_->WatchConnectedNetwork(
      fit::bind_member(this, &NetworkProvisioningServerDelegateImpl::OnWlanNetworkUpdate));
}

WEAVE_ERROR NetworkProvisioningServerDelegateImpl::GetWiFiStationProvision(
    DeviceNetworkInfo& net_info, bool include_credentials) {
  // Start copying provision info.
  net_info.Reset();

  if (current_network_config_.IsEmpty() || !current_network_config_.has_id()) {
    return WEAVE_ERROR_INCORRECT_STATE;
  }

  if (sizeof(net_info.WiFiSSID) < current_network_config_.id().ssid.size()) {
    return WEAVE_ERROR_BUFFER_TOO_SMALL;
  }

  net_info.NetworkId = kWiFiStationNetworkId;
  net_info.FieldPresent.NetworkId = true;
  net_info.NetworkType = kNetworkType_WiFi;
  std::copy(current_network_config_.id().ssid.begin(), current_network_config_.id().ssid.end(),
            net_info.WiFiSSID);
  net_info.WiFiMode = kWiFiMode_Managed;
  net_info.WiFiRole = kWiFiRole_Station;
  net_info.WiFiSecurityType = ConvertToWiFiSecurityType(current_network_config_.id().type);

  if (!include_credentials || !current_network_config_.has_credential()) {
    return WEAVE_NO_ERROR;
  }

  switch (current_network_config_.credential().Which()) {
    case fuchsia::wlan::policy::Credential::Tag::kNone:
      break;
    case fuchsia::wlan::policy::Credential::Tag::kPassword:
      net_info.WiFiKeyLen = current_network_config_.credential().password().size();
      if (net_info.WiFiKeyLen > DeviceNetworkInfo::kMaxWiFiKeyLength) {
        return WEAVE_ERROR_BUFFER_TOO_SMALL;
      }
      std::copy(current_network_config_.credential().password().begin(),
                current_network_config_.credential().password().end(), net_info.WiFiKey);
      break;
    case fuchsia::wlan::policy::Credential::Tag::kPsk:
      net_info.WiFiKeyLen = current_network_config_.credential().psk().size();
      if (net_info.WiFiKeyLen > DeviceNetworkInfo::kMaxWiFiKeyLength) {
        return WEAVE_ERROR_BUFFER_TOO_SMALL;
      }
      std::copy(current_network_config_.credential().psk().begin(),
                current_network_config_.credential().psk().end(), net_info.WiFiKey);
      break;
    default:
      FX_LOGS(ERROR) << "Unknown WLAN credential type: "
                     << current_network_config_.credential().Which();
      return WEAVE_ERROR_UNSUPPORTED_WEAVE_FEATURE;
  }

  return WEAVE_NO_ERROR;
}

nl::Weave::Profiles::NetworkProvisioning::WiFiSecurityType
NetworkProvisioningServerDelegateImpl::ConvertToWiFiSecurityType(
    fuchsia::wlan::policy::SecurityType type) {
  switch (type) {
    case fuchsia::wlan::policy::SecurityType::NONE:
      return kWiFiSecurityType_None;
    case fuchsia::wlan::policy::SecurityType::WEP:
      return kWiFiSecurityType_WEP;
    case fuchsia::wlan::policy::SecurityType::WPA:
      return kWiFiSecurityType_WPAPersonal;
    case fuchsia::wlan::policy::SecurityType::WPA2:
      return kWiFiSecurityType_WPA2Personal;
    case fuchsia::wlan::policy::SecurityType::WPA3:
      return kWiFiSecurityType_WPA3Personal;
    default:
      return kWiFiSecurityType_NotSpecified;
  }
}

}  // namespace Internal
}  // namespace DeviceLayer
}  // namespace Weave
}  // namespace nl
