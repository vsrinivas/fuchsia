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

using namespace ::nl;
using namespace ::nl::Weave;
using namespace ::nl::Weave::TLV;
using namespace ::nl::Weave::Profiles::Common;
using namespace ::nl::Weave::Profiles::NetworkProvisioning;

using Profiles::kWeaveProfile_Common;

namespace nl {
namespace Weave {
namespace DeviceLayer {
namespace Internal {

NetworkProvisioningServerImpl NetworkProvisioningServerImpl::sInstance;

WEAVE_ERROR NetworkProvisioningServerImpl::_Init(void) {
  return GenericNetworkProvisioningServerImpl<NetworkProvisioningServerImpl>::DoInit();
}

void NetworkProvisioningServerImpl::_OnPlatformEvent(const WeaveDeviceEvent* event) {
  // Propagate the event to the GenericNetworkProvisioningServerImpl<> base class so
  // that it can take action on specific events.
  GenericImplClass::_OnPlatformEvent(event);
}

WEAVE_ERROR NetworkProvisioningServerImpl::GetWiFiStationProvision(NetworkInfo& netInfo,
                                                                   bool includeCredentials) {
  return WEAVE_NO_ERROR;
}

WEAVE_ERROR NetworkProvisioningServerImpl::SetWiFiStationProvision(const NetworkInfo& netInfo) {
  return WEAVE_NO_ERROR;
}

WEAVE_ERROR NetworkProvisioningServerImpl::ClearWiFiStationProvision(void) {
  return WEAVE_NO_ERROR;
}

WEAVE_ERROR NetworkProvisioningServerImpl::InitiateWiFiScan(void) { return WEAVE_NO_ERROR; }

void NetworkProvisioningServerImpl::HandleScanDone() {}

#if WEAVE_DEVICE_CONFIG_WIFI_SCAN_COMPLETION_TIMEOUT

void NetworkProvisioningServerImpl::HandleScanTimeOut(::nl::Weave::System::Layer* aLayer,
                                                      void* aAppState,
                                                      ::nl::Weave::System::Error aError) {}

#endif  // WEAVE_DEVICE_CONFIG_WIFI_SCAN_COMPLETION_TIMEOUT

bool NetworkProvisioningServerImpl::IsSupportedWiFiSecurityType(WiFiSecurityType_t wifiSecType) {
  return false;
}

}  // namespace Internal
}  // namespace DeviceLayer
}  // namespace Weave
}  // namespace nl
