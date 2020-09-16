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
// clang-format on
#include "connectivity_manager_delegate_impl.h"

#include <lib/syslog/cpp/macros.h>

namespace nl {
namespace Weave {
namespace DeviceLayer {

namespace {

using namespace ::nl;
using namespace ::nl::Weave;
using namespace ::nl::Weave::TLV;
using namespace ::nl::Weave::Profiles::Common;
using namespace ::nl::Weave::Profiles::NetworkProvisioning;

using namespace ::nl::Weave::Profiles::WeaveTunnel;
using namespace ::nl::Weave::DeviceLayer::Internal;

using Profiles::kWeaveProfile_Common;
using Profiles::kWeaveProfile_NetworkProvisioning;

}  // unnamed namespace

bool ConnectivityManagerDelegateImpl::IsServiceTunnelConnected(void) {
  WeaveTunnelAgent::AgentState tunnel_state = ServiceTunnelAgent.GetWeaveTunnelAgentState();
  return (tunnel_state == WeaveTunnelAgent::kState_PrimaryTunModeEstablished ||
          tunnel_state == WeaveTunnelAgent::kState_PrimaryAndBkupTunModeEstablished ||
          tunnel_state == WeaveTunnelAgent::kState_BkupOnlyTunModeEstablished);
}

bool ConnectivityManagerDelegateImpl::IsServiceTunnelRestricted(void) {
  return ServiceTunnelAgent.IsTunnelRoutingRestricted();
}

void ConnectivityManagerDelegateImpl::HandleServiceTunnelNotification(
    WeaveTunnelConnectionMgr::TunnelConnNotifyReasons reason, WEAVE_ERROR err, void* app_ctx) {
  ConnectivityManagerDelegateImpl* delegate = (ConnectivityManagerDelegateImpl*)app_ctx;
  bool new_tunnel_state = false;
  bool prev_tunnel_state = GetFlag(delegate->flags_, kFlag_ServiceTunnelUp);
  bool is_restricted = false;

  switch (reason) {
    case WeaveTunnelConnectionMgr::kStatus_TunDown:
      new_tunnel_state = false;
      FX_LOGS(INFO) << "Service tunnel down.";
      break;
    case WeaveTunnelConnectionMgr::kStatus_TunPrimaryConnError:
      new_tunnel_state = false;
      FX_LOGS(ERROR) << "Service tunnel connection error: " << ::nl::ErrorStr(err);
      break;
    case WeaveTunnelConnectionMgr::kStatus_TunPrimaryUp:
      new_tunnel_state = true;
      is_restricted = (err == WEAVE_ERROR_TUNNEL_ROUTING_RESTRICTED);
      FX_LOGS(INFO) << "Service tunnel established, restricted: " << is_restricted;
      break;
    default:
      break;
  }

  // Ignore event if the tunnel state has not changed.
  if (new_tunnel_state == prev_tunnel_state) {
    return;
  }

  // Update the cached copy of the state.
  SetFlag(delegate->flags_, kFlag_ServiceTunnelUp, new_tunnel_state);

  // Alert other components of the change to the tunnel state.
  WeaveDeviceEvent tunnel_event;
  tunnel_event.Type = DeviceEventType::kServiceTunnelStateChange;
  tunnel_event.ServiceTunnelStateChange.Result =
      GetConnectivityChange(prev_tunnel_state, new_tunnel_state);
  tunnel_event.ServiceTunnelStateChange.IsRestricted = is_restricted;
  PlatformMgrImpl().PostEvent(&tunnel_event);

  // If the new tunnel state represents a logical change in connectivity to the
  // service, as it relates to the application, post a ServiceConnectivityChange
  // event.
  // Establishment of a restricted tunnel to service does not constitute a
  // logical change in connectivity from an application's perspective, so ignore
  // the event in that case.
  if (new_tunnel_state && is_restricted) {
    return;
  }
  WeaveDeviceEvent service_event;
  service_event.Type = DeviceEventType::kServiceConnectivityChange;
  service_event.ServiceConnectivityChange.ViaTunnel.Result =
      (new_tunnel_state) ? kConnectivity_Established : kConnectivity_Lost;
  service_event.ServiceConnectivityChange.ViaThread.Result = kConnectivity_NoChange;
  service_event.ServiceConnectivityChange.Overall.Result =
      ConnectivityMgr().HaveServiceConnectivityViaThread()
          ? kConnectivity_NoChange
          : service_event.ServiceConnectivityChange.ViaTunnel.Result;
  PlatformMgrImpl().PostEvent(&service_event);
}

void ConnectivityManagerDelegateImpl::StartServiceTunnel(void) {
  if (GetFlag(flags_, kFlag_ServiceTunnelStarted)) {
    return;
  }
  // Update the tunnel started state.
  FX_LOGS(INFO) << "Starting service tunnel";
  WEAVE_ERROR err = ServiceTunnelAgent.StartServiceTunnel();
  if (err != WEAVE_NO_ERROR) {
    FX_LOGS(ERROR) << "StartServiceTunnel() failed: " << nl::ErrorStr(err);
    return;
  }
  SetFlag(flags_, kFlag_ServiceTunnelStarted, true);
}

void ConnectivityManagerDelegateImpl::StopServiceTunnel(void) {
  if (GetFlag(flags_, kFlag_ServiceTunnelStarted) == false) {
    return;
  }
  // Update the tunnel started state.
  SetFlag(flags_, kFlag_ServiceTunnelStarted, false);
  FX_LOGS(INFO) << "Stopping service tunnel";
  ServiceTunnelAgent.StopServiceTunnel();
}

void ConnectivityManagerDelegateImpl::StopServiceTunnel(WEAVE_ERROR err) {
  ClearFlag(flags_, kFlag_ServiceTunnelStarted);
  ServiceTunnelAgent.StopServiceTunnel(err);
}

WEAVE_ERROR ConnectivityManagerDelegateImpl::Init() {
  WEAVE_ERROR err = WEAVE_NO_ERROR;

  // Reset internal flags to clear any pre-existing state.
  service_tunnel_mode_ = ConnectivityManager::kServiceTunnelMode_Enabled;
  flags_ = 0;

  // Initialize the Weave Addressing and Routing Module.
  err = Warm::Init(FabricState);
  if (err != WEAVE_NO_ERROR) {
    FX_LOGS(ERROR) << "Warm Init failed : " << nl::ErrorStr(err);
    return err;
  }

  // Initialize tunnel agent.
  err = InitServiceTunnelAgent();
  if (err != WEAVE_NO_ERROR) {
    FX_LOGS(ERROR) << "InitServiceTunnelAgent failed : " << nl::ErrorStr(err);
    return err;
  }

  // Bind tunnel notification handler.
  ServiceTunnelAgent.OnServiceTunStatusNotify = HandleServiceTunnelNotification;
  return err;
}

WEAVE_ERROR ConnectivityManagerDelegateImpl::InitServiceTunnelAgent() {
  return Internal::InitServiceTunnelAgent(this);
}

bool ConnectivityManagerDelegateImpl::ShouldStartServiceTunnel() {
  return (service_tunnel_mode_ == ConnectivityManager::kServiceTunnelMode_Enabled &&
          ConfigurationMgr().IsMemberOfFabric() && ConfigurationMgr().IsServiceProvisioned());
}

void ConnectivityManagerDelegateImpl::OnPlatformEvent(const WeaveDeviceEvent* event) {
  if (event == NULL) {
    return;
  }
  switch (event->Type) {
    case DeviceEventType::kFabricMembershipChange:
    case DeviceEventType::kServiceProvisioningChange:
      if (ShouldStartServiceTunnel()) {
        StartServiceTunnel();
      } else {
        StopServiceTunnel();
      }
      break;
    case DeviceEventType::kAccountPairingChange:
      // When account pairing successfully completes, if the tunnel to the
      // service is subject to routing restrictions (imposed because at the time
      // the tunnel was established the device was not paired to an account)
      // then force the tunnel to close.  This will result in the tunnel being
      // re-established, which should lift the service-side restrictions.
      if (event->AccountPairingChange.IsPairedToAccount &&
          GetFlag(flags_, kFlag_ServiceTunnelStarted) && IsServiceTunnelRestricted()) {
        FX_LOGS(INFO) << "Restarting service tunnel to lift routing restrictions";
        StopServiceTunnel(WEAVE_ERROR_TUNNEL_FORCE_ABORT);
        StartServiceTunnel();
      }
      break;
    default:
      break;
  }
}

}  // namespace DeviceLayer
}  // namespace Weave
}  // namespace nl
