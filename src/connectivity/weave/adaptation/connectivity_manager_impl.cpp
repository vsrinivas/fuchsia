// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#include <Weave/DeviceLayer/ConnectivityManager.h>
#include <Weave/DeviceLayer/internal/BLEManager.h>
#include <Weave/DeviceLayer/internal/DeviceNetworkInfo.h>
#include <Weave/DeviceLayer/internal/NetworkProvisioningServer.h>
#include <Weave/DeviceLayer/internal/ServiceTunnelAgent.h>
#include <Weave/Profiles/WeaveProfiles.h>
#include <Weave/Profiles/common/CommonProfile.h>
#include <Warm/Warm.h>

#if WEAVE_DEVICE_CONFIG_ENABLE_WOBLE
#include <Weave/DeviceLayer/internal/GenericConnectivityManagerImpl_BLE.ipp>
#endif

#if WEAVE_DEVICE_CONFIG_ENABLE_WIFI_TELEMETRY
#include <Weave/Support/TraitEventUtils.h>
#include <nest/trait/network/TelemetryNetworkTrait.h>
#include <nest/trait/network/TelemetryNetworkWifiTrait.h>
#endif
// clang-format on

using namespace ::nl;
using namespace ::nl::Weave;
using namespace ::nl::Weave::TLV;
using namespace ::nl::Weave::Profiles::Common;
using namespace ::nl::Weave::Profiles::NetworkProvisioning;
// using namespace ::nl::Weave::Profiles::WeaveTunnel;
using namespace ::nl::Weave::DeviceLayer::Internal;

using Profiles::kWeaveProfile_Common;
using Profiles::kWeaveProfile_NetworkProvisioning;

namespace nl {
namespace Weave {
namespace DeviceLayer {

ConnectivityManagerImpl ConnectivityManagerImpl::sInstance;

ConnectivityManager::WiFiStationMode ConnectivityManagerImpl::_GetWiFiStationMode(void) {
  return kWiFiStationMode_Enabled;
}

bool ConnectivityManagerImpl::_IsWiFiStationEnabled(void) { return true; }

WEAVE_ERROR ConnectivityManagerImpl::_SetWiFiStationMode(WiFiStationMode val) {
  return WEAVE_NO_ERROR;
}

bool ConnectivityManagerImpl::_IsWiFiStationProvisioned(void) { return true; }

void ConnectivityManagerImpl::_ClearWiFiStationProvision(void) {}

WEAVE_ERROR ConnectivityManagerImpl::_SetWiFiAPMode(WiFiAPMode val) { return WEAVE_NO_ERROR; }

void ConnectivityManagerImpl::_DemandStartWiFiAP(void) {}

void ConnectivityManagerImpl::_StopOnDemandWiFiAP(void) {}

void ConnectivityManagerImpl::_MaintainOnDemandWiFiAP(void) {}

void ConnectivityManagerImpl::_SetWiFiAPIdleTimeoutMS(uint32_t val) {}

WEAVE_ERROR ConnectivityManagerImpl::_GetAndLogWifiStatsCounters(void) { return WEAVE_NO_ERROR; }

WEAVE_ERROR ConnectivityManagerImpl::_SetServiceTunnelMode(ServiceTunnelMode val) {
  return WEAVE_NO_ERROR;
}

bool ConnectivityManagerImpl::_IsServiceTunnelConnected(void) { return true; }

bool ConnectivityManagerImpl::_IsServiceTunnelRestricted(void) { return true; }

bool ConnectivityManagerImpl::_HaveServiceConnectivityViaTunnel(void) { return true; }

// ==================== ConnectivityManager Platform Internal Methods ====================

WEAVE_ERROR ConnectivityManagerImpl::_Init() { return WEAVE_NO_ERROR; }

void ConnectivityManagerImpl::_OnPlatformEvent(const WeaveDeviceEvent* event) {}

void ConnectivityManagerImpl::_OnWiFiScanDone() {}

void ConnectivityManagerImpl::_OnWiFiStationProvisionChange() {}

// ==================== ConnectivityManager Private Methods ====================

}  // namespace DeviceLayer
}  // namespace Weave
}  // namespace nl
