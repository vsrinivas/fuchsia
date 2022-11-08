// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#include <Weave/DeviceLayer/ConnectivityManager.h>
#include <Weave/DeviceLayer/ThreadStackManager.h>
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

#include <fuchsia/hardware/network/cpp/fidl.h>
#include <fuchsia/net/interfaces/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

#include "weave_inspector.h"

namespace nl::Weave::DeviceLayer {

namespace {

using namespace ::nl::Weave;
using namespace ::nl::Weave::Profiles::WeaveTunnel;

using fuchsia::hardware::network::DeviceClass;
using Internal::ServiceTunnelAgent;
using nl::Weave::WeaveInspector;

using ThreadMode = ConnectivityManager::ThreadMode;

constexpr ThreadMode kThreadMode_NotSupported = ConnectivityManager::kThreadMode_NotSupported;
constexpr ThreadMode kThreadMode_Disabled = ConnectivityManager::kThreadMode_Disabled;
constexpr ThreadMode kThreadMode_Enabled = ConnectivityManager::kThreadMode_Enabled;

// Returns a pointer to the ServiceTunnelAgent instance.
WeaveTunnelAgent* SrvTunnelAgent() { return &ServiceTunnelAgent; }

}  // unnamed namespace

bool ConnectivityManagerDelegateImpl::IsServiceTunnelConnected() {
  WeaveTunnelAgent::AgentState tunnel_state = SrvTunnelAgent()->GetWeaveTunnelAgentState();
  return (tunnel_state == WeaveTunnelAgent::kState_PrimaryTunModeEstablished ||
          tunnel_state == WeaveTunnelAgent::kState_PrimaryAndBkupTunModeEstablished ||
          tunnel_state == WeaveTunnelAgent::kState_BkupOnlyTunModeEstablished);
}

bool ConnectivityManagerDelegateImpl::IsServiceTunnelRestricted() {
  return SrvTunnelAgent()->IsTunnelRoutingRestricted();
}

void ConnectivityManagerDelegateImpl::HandleServiceTunnelNotification(
    WeaveTunnelConnectionMgr::TunnelConnNotifyReasons reason, WEAVE_ERROR err, void* app_ctx) {
  ConnectivityManagerDelegateImpl* delegate =
      static_cast<ConnectivityManagerDelegateImpl*>(app_ctx);
  bool new_tunnel_state = false;
  bool prev_tunnel_state = GetFlag(delegate->flags_, kFlag_ServiceTunnelUp);
  bool is_restricted = false;
  auto& inspector = WeaveInspector::GetWeaveInspector();
  switch (reason) {
    case WeaveTunnelConnectionMgr::kStatus_TunDown:
      new_tunnel_state = false;
      FX_LOGS(INFO) << "Service tunnel down.";
      inspector.NotifyTunnelStateChange(WeaveInspector::kTunnelState_NoTunnel,
                                        WeaveInspector::kTunnelType_None, is_restricted);
      break;
    case WeaveTunnelConnectionMgr::kStatus_TunPrimaryConnError:
      new_tunnel_state = false;
      FX_LOGS(ERROR) << "Service tunnel connection error: " << ::nl::ErrorStr(err);
      inspector.NotifyTunnelStateChange(WeaveInspector::kTunnelState_NoTunnel,
                                        WeaveInspector::kTunnelType_None, is_restricted);
      break;
    case WeaveTunnelConnectionMgr::kStatus_TunPrimaryUp:
      new_tunnel_state = true;
      is_restricted = (err == WEAVE_ERROR_TUNNEL_ROUTING_RESTRICTED);
      FX_LOGS(INFO) << "Service tunnel established, restricted: " << is_restricted;
      inspector.NotifyTunnelStateChange(WeaveInspector::kTunnelState_PrimaryTunMode,
                                        WeaveInspector::kTunnelType_Primary, is_restricted);
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
  if (!GetFlag(flags_, kFlag_ServiceTunnelStarted)) {
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
  state_.set_error_handler([](zx_status_t status) {
    // Treat connection loss to netstack as fatal and inform applications that
    // they should attempt a graceful shutdown / restart.
    FX_LOGS(ERROR) << "Disconnected from netstack: " << zx_status_get_string(status);
    const WeaveDeviceEvent shutdown_request = {
        .Type = WeaveDevicePlatformEventType::kShutdownRequest,
    };
    PlatformMgrImpl().PostEvent(&shutdown_request);
  });

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
    // Forcefully abort the tunnel when the device loses connectivity as this
    // ensures the proper release of resources like timers. This stops tunnel
    // retry mechanism when there is no connectivity. The tunnel will be
    // restarted explicitly, when the connectivity is restored.
    StopServiceTunnel(WEAVE_ERROR_TUNNEL_FORCE_ABORT);
  }
}

void ConnectivityManagerDelegateImpl::OnPlatformEvent(const WeaveDeviceEvent* event) {
  if (event == nullptr) {
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

WEAVE_ERROR ConnectivityManagerDelegateImpl::RefreshEndpoints() {
  return MessageLayer.RefreshEndpoints();
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
    if (event.is_changed() && properties.has_online()) {
      PlatformMgr().ScheduleWork(
          [](intptr_t context) {
            ConnectivityManagerDelegateImpl* delegate = (ConnectivityManagerDelegateImpl*)context;
            WEAVE_ERROR err = delegate->RefreshEndpoints();
            if (err != WEAVE_NO_ERROR) {
              FX_LOGS(ERROR) << "MessageLayer.RefreshEndpoints() failed: " << nl::ErrorStr(err);
            }
          },
          (intptr_t)this);
    }

    // TODO(73097): Instead of attempting to "detect" the right interface (and assuming the first
    // interface that matches is correct), allow the wlan and thread interfaces to be specified via
    // config.

    if (wlan_interface_id_ == 0 && properties.has_device_class() &&
        properties.device_class().is_device() &&
        properties.device_class().device() == DeviceClass::WLAN) {
      wlan_interface_id_ = properties.id();
      wlan_interface_name_ = properties.name();
      FX_LOGS(INFO) << "Identifying interface \"" << properties.name()
                    << "\" as the WLAN interface.";
      PlatformMgr().ScheduleWork(
          [](intptr_t) { Warm::WiFiInterfaceStateChange(Warm::kInterfaceStateUp); }, 0);
    }

    // The thread interface ID is recorded, but WARM is explicitly not signaled.
    // This is deferred to the ThreadStackManager, which notifies WARM when the
    // fuchsia.lowpan.device FIDL service reports that a new device was added.
    // This distinction is required as LoWPAN may create a network device and
    // trigger this interface update before it registers the LoWPAN device for
    // it's clients, resulting in an inconsistent state where WARM believes the
    // interface is up, but LoWPAN claims it is unaware of the device.
    if (thread_interface_id_ == 0 && properties.has_name() &&
        properties.name() == ThreadStackMgrImpl().GetInterfaceName()) {
      thread_interface_id_ = properties.id();
      FX_LOGS(INFO) << "Identifying interface \"" << properties.name()
                    << "\" as the Thread interface.";
    }
  } else if (event.is_removed()) {
    routable_v4_interfaces.erase(event.removed());
    routable_v6_interfaces.erase(event.removed());

    if (event.removed() == wlan_interface_id_) {
      FX_LOGS(INFO) << "Wlan iface removed, informing WARM of WLAN down.";
      PlatformMgr().ScheduleWork(
          [](intptr_t context) {
            auto self = reinterpret_cast<ConnectivityManagerDelegateImpl*>(context);
            Warm::WiFiInterfaceStateChange(Warm::kInterfaceStateDown);
            self->wlan_interface_id_ = 0;
            self->wlan_interface_name_ = std::nullopt;
          },
          reinterpret_cast<intptr_t>(this));
    } else if (event.removed() == thread_interface_id_) {
      PlatformMgr().ScheduleWork(
          [](intptr_t context) {
            auto self = reinterpret_cast<ConnectivityManagerDelegateImpl*>(context);
            self->thread_interface_id_ = 0;
          },
          reinterpret_cast<intptr_t>(this));
    }
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

std::optional<std::string> ConnectivityManagerDelegateImpl::GetWiFiInterfaceName() {
  return wlan_interface_name_;
}

ThreadMode ConnectivityManagerDelegateImpl::GetThreadMode() {
  if (!ThreadStackMgrImpl().IsThreadSupported()) {
    return kThreadMode_NotSupported;
  }

  return ((ThreadStackMgrImpl()._IsThreadEnabled()) ? (kThreadMode_Enabled)
                                                    : (kThreadMode_Disabled));
}

fuchsia::net::interfaces::admin::ControlSyncPtr*
ConnectivityManagerDelegateImpl::GetTunInterfaceControlSyncPtr() {
  return SrvTunnelAgent()->GetInterfaceControlSyncPtr();
}

}  // namespace nl::Weave::DeviceLayer
