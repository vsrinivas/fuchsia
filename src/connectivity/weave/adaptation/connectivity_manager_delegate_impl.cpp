// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#include <Weave/DeviceLayer/ConnectivityManager.h>
#include <Weave/DeviceLayer/internal/BLEManager.h>
#include <Weave/DeviceLayer/internal/DeviceNetworkInfo.h>
#include <Weave/DeviceLayer/internal/ServiceTunnelAgent.h>
#include <Weave/Profiles/WeaveProfiles.h>
#include <Warm/Warm.h>

#if WEAVE_DEVICE_CONFIG_ENABLE_WOBLE
#include <Weave/DeviceLayer/internal/GenericConnectivityManagerImpl_BLE.ipp>
#endif
// clang-format on
#include "connectivity_manager_delegate_impl.h"

#include <fuchsia/net/interfaces/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

namespace nl {
namespace Weave {
namespace DeviceLayer {

namespace {

using namespace ::nl::Weave;
using namespace ::nl::Weave::Profiles::WeaveTunnel;

using Internal::ServiceTunnelAgent;

// Returns a pointer to the ServiceTunnelAgent instance.
WeaveTunnelAgent* SrvTunnelAgent() { return &ServiceTunnelAgent; }

}  // unnamed namespace

bool ConnectivityManagerDelegateImpl::IsServiceTunnelConnected(void) {
  WeaveTunnelAgent::AgentState tunnel_state = SrvTunnelAgent()->GetWeaveTunnelAgentState();
  return (tunnel_state == WeaveTunnelAgent::kState_PrimaryTunModeEstablished ||
          tunnel_state == WeaveTunnelAgent::kState_PrimaryAndBkupTunModeEstablished ||
          tunnel_state == WeaveTunnelAgent::kState_BkupOnlyTunModeEstablished);
}

bool ConnectivityManagerDelegateImpl::IsServiceTunnelRestricted(void) {
  return SrvTunnelAgent()->IsTunnelRoutingRestricted();
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

void ConnectivityManagerDelegateImpl::StartServiceTunnel() {
  if (GetFlag(flags_, kFlag_ServiceTunnelStarted)) {
    return;
  }
  // Update the tunnel started state.
  FX_LOGS(INFO) << "Starting service tunnel";
  WEAVE_ERROR err = SrvTunnelAgent()->StartServiceTunnel();
  if (err != WEAVE_NO_ERROR) {
    FX_LOGS(ERROR) << "StartServiceTunnel() failed: " << nl::ErrorStr(err);
    return;
  }
  SetFlag(flags_, kFlag_ServiceTunnelStarted, true);
}

void ConnectivityManagerDelegateImpl::StopServiceTunnel(WEAVE_ERROR err) {
  if (GetFlag(flags_, kFlag_ServiceTunnelStarted) == false) {
    return;
  }
  // Update the tunnel started state.
  FX_LOGS(INFO) << "Stopping service tunnel";
  SrvTunnelAgent()->StopServiceTunnel(err);
  SetFlag(flags_, kFlag_ServiceTunnelStarted, false);
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
  SrvTunnelAgent()->OnServiceTunStatusNotify = HandleServiceTunnelNotification;

  // Watch for interface updates.
  err = PlatformMgrImpl().GetComponentContextForProcess()->svc()->Connect(state_.NewRequest());
  if (err != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to register state watcher." << zx_status_get_string(err);
    return err;
  }

  fuchsia::net::interfaces::WatcherOptions options;
  state_->GetWatcher(std::move(options), watcher_.NewRequest());
  if (!watcher_.is_bound()) {
    FX_LOGS(ERROR) << "Failed to bind watcher.";
    return WEAVE_ERROR_INCORRECT_STATE;
  }
  watcher_->Watch(fit::bind_member(this, &ConnectivityManagerDelegateImpl::OnInterfaceEvent));
  return err;
}

WEAVE_ERROR ConnectivityManagerDelegateImpl::InitServiceTunnelAgent() {
  return Internal::InitServiceTunnelAgent(this);
}

void ConnectivityManagerDelegateImpl::DriveServiceTunnelState() {
  bool should_start_service_tunnel =
      (service_tunnel_mode_ == ConnectivityManager::kServiceTunnelMode_Enabled) &&
      GetFlag(flags_, kFlag_HaveIPv4InternetConnectivity | kFlag_HaveIPv6InternetConnectivity) &&
      ConfigurationMgr().IsMemberOfFabric() && ConfigurationMgr().IsServiceProvisioned();

  if (should_start_service_tunnel == GetFlag(flags_, kFlag_ServiceTunnelStarted)) {
    return;
  }

  if (should_start_service_tunnel) {
    StartServiceTunnel();
  } else {
    StopServiceTunnel(WEAVE_NO_ERROR);
  }
}

void ConnectivityManagerDelegateImpl::OnPlatformEvent(const WeaveDeviceEvent* event) {
  if (event == NULL) {
    return;
  }
  switch (event->Type) {
    case DeviceEventType::kFabricMembershipChange:
    case DeviceEventType::kServiceProvisioningChange:
      DriveServiceTunnelState();
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

void ConnectivityManagerDelegateImpl::OnInterfaceEvent(fuchsia::net::interfaces::Event event) {
  fuchsia::net::interfaces::Properties properties;
  bool properties_event = true;

  if (event.is_existing()) {
    properties = std::move(event.existing());
  } else if (event.is_added()) {
    properties = std::move(event.added());
  } else if (event.is_changed()) {
    properties = std::move(event.changed());
  } else {
    properties_event = false;
  }

  // If a properties event, record the ID of the interface into the
  // corresponding interface list. If removed or the route was lost,
  // remove it from the interface list.
  if (properties_event) {
    if (properties.has_has_default_ipv4_route()) {
      if (properties.has_default_ipv4_route()) {
        routable_v4_interfaces.insert(properties.id());
      } else {
        routable_v4_interfaces.erase(properties.id());
      }
    }
    if (properties.has_has_default_ipv6_route()) {
      if (properties.has_default_ipv6_route()) {
        routable_v6_interfaces.insert(properties.id());
      } else {
        routable_v6_interfaces.erase(properties.id());
      }
    }
  } else if (event.is_removed()) {
    routable_v4_interfaces.erase(event.removed());
    routable_v6_interfaces.erase(event.removed());
  }

  PlatformMgr().ScheduleWork(
      [](intptr_t context) {
        ConnectivityManagerDelegateImpl* delegate = (ConnectivityManagerDelegateImpl*)context;
        SetFlag(delegate->flags_, kFlag_HaveIPv4InternetConnectivity,
                !delegate->routable_v4_interfaces.empty());
        SetFlag(delegate->flags_, kFlag_HaveIPv6InternetConnectivity,
                !delegate->routable_v6_interfaces.empty());
        delegate->DriveServiceTunnelState();
      },
      (intptr_t)this);

  watcher_->Watch(fit::bind_member(this, &ConnectivityManagerDelegateImpl::OnInterfaceEvent));
}

}  // namespace DeviceLayer
}  // namespace Weave
}  // namespace nl
