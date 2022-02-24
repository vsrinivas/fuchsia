// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#include <Weave/DeviceLayer/ConnectivityManager.h>
#include <Weave/DeviceLayer/ThreadStackManager.h>
#include <Warm/Warm.h>
// clang-format on

#include <fuchsia/net/cpp/fidl.h>
#include <fuchsia/net/interfaces/cpp/fidl.h>
#include <fuchsia/net/stack/cpp/fidl.h>
#include <fuchsia/netstack/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <netinet/ip6.h>

#include <optional>

// ==================== WARM Platform Functions ====================

namespace nl {
namespace Weave {
namespace Warm {
namespace Platform {

namespace {
using DeviceLayer::ConnectivityMgrImpl;
using DeviceLayer::ThreadStackMgrImpl;

// Fixed name for tunnel interface.
constexpr char kTunInterfaceName[] = "weav-tun0";

// Route metric values for primary and backup tunnels. Higher priority tunnels
// have lower metric values so that they are prioritized in the routing table.
constexpr uint32_t kRouteMetric_HighPriority = 0;
constexpr uint32_t kRouteMetric_MediumPriority = 99;
constexpr uint32_t kRouteMetric_LowPriority = 999;

// Returns the interface name associated with the given interface type.
// Unsupported interface types will not populate the optional.
std::optional<std::string> GetInterfaceName(InterfaceType interface_type) {
  switch (interface_type) {
    case kInterfaceTypeThread:
      return ThreadStackMgrImpl().GetInterfaceName();
    case kInterfaceTypeTunnel:
      return kTunInterfaceName;
    case kInterfaceTypeWiFi:
      return ConnectivityMgrImpl().GetWiFiInterfaceName();
    default:
      FX_LOGS(ERROR) << "Unknown interface type: " << interface_type;
      return std::nullopt;
  }
}

// Returns whether IPv6 forwarding is allowed for the given interface type.
bool ShouldEnableV6Forwarding(InterfaceType interface_type) {
  return (interface_type == kInterfaceTypeThread) || (interface_type == kInterfaceTypeTunnel);
}

// Returns the interface id associated with the given interface name. On
// failures to fetch the list, no value will be returned. When the interface
// does not exist, the interface ID '0' will be returned (it is guaranteed by
// the networking stack that all valid interface IDs are positive).
std::optional<uint64_t> GetInterfaceId(std::string interface_name) {
  fuchsia::net::interfaces::StateSyncPtr state_sync_ptr;
  fuchsia::net::interfaces::WatcherOptions options;
  fuchsia::net::interfaces::WatcherSyncPtr watcher_sync_ptr;

  auto svc = nl::Weave::DeviceLayer::PlatformMgrImpl().GetComponentContextForProcess()->svc();
  zx_status_t status = svc->Connect(state_sync_ptr.NewRequest());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to bind state protocol: " << zx_status_get_string(status);
    return std::nullopt;
  }

  state_sync_ptr->GetWatcher(std::move(options), watcher_sync_ptr.NewRequest());
  if (!watcher_sync_ptr.is_bound()) {
    FX_LOGS(ERROR) << "Failed to bind watcher.";
    return std::nullopt;
  }

  fuchsia::net::interfaces::Event event;
  do {
    status = watcher_sync_ptr->Watch(&event);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to watch for interface event: " << zx_status_get_string(status);
      return std::nullopt;
    }
    // If the entry is not of type `existing`, it is not part of the initial
    // list of interfaces that was present when the channel was initialized.
    if (!event.is_existing()) {
      continue;
    }
    // Check if the event matches the provided interface name and if so, return
    // the interface ID.
    if (event.existing().name() == interface_name) {
      return event.existing().id();
    }
  } while (!event.is_idle());
  return 0;
}

std::string_view StackErrorToString(fuchsia::net::stack::Error error) {
  switch (error) {
    case fuchsia::net::stack::Error::INTERNAL:
      return "internal";
    case fuchsia::net::stack::Error::NOT_SUPPORTED:
      return "not supported";
    case fuchsia::net::stack::Error::INVALID_ARGS:
      return "invalid arguments";
    case fuchsia::net::stack::Error::BAD_STATE:
      return "bad state";
    case fuchsia::net::stack::Error::TIME_OUT:
      return "timeout";
    case fuchsia::net::stack::Error::NOT_FOUND:
      return "not found";
    case fuchsia::net::stack::Error::ALREADY_EXISTS:
      return "already exists";
    case fuchsia::net::stack::Error::IO:
      return "i/o";
  }
}

// Add or remove an address attached to the Thread or WLAN interfaces.
PlatformResult AddRemoveAddressInternal(InterfaceType interface_type,
                                        const Inet::IPAddress &address, uint8_t prefix_length,
                                        bool add) {
  auto svc = nl::Weave::DeviceLayer::PlatformMgrImpl().GetComponentContextForProcess()->svc();

  // Determine interface name to add/remove from.
  std::optional<std::string> interface_name = GetInterfaceName(interface_type);
  if (!interface_name) {
    return kPlatformResultFailure;
  }

  fuchsia::net::stack::StackSyncPtr net_stack_sync_ptr;
  zx_status_t status = svc->Connect(net_stack_sync_ptr.NewRequest());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to connect to netstack: " << zx_status_get_string(status);
    return kPlatformResultFailure;
  }

  // Determine the interface ID to add/remove from.
  std::optional<uint64_t> interface_id = GetInterfaceId(interface_name.value());
  if (!add && interface_id && interface_id.value() == 0) {
    // When removing, don't report an error if the interface wasn't found. The
    // interface may already have been removed at this point.
    FX_LOGS(INFO) << "Interface " << interface_name.value() << " has already been removed.";
    return kPlatformResultSuccess;
  } else if (!interface_id) {
    return kPlatformResultFailure;
  }

  // Construct the IP address for the interface.
  fuchsia::net::IpAddress ip_addr;
  fuchsia::net::Ipv6Address ipv6_addr;

  std::memcpy(ipv6_addr.addr.data(), (uint8_t *)(address.Addr), ipv6_addr.addr.size());
  ip_addr.set_ipv6(ipv6_addr);

  fuchsia::net::Subnet subnet{
      .addr = std::move(ip_addr),
      .prefix_len = prefix_length,
  };

  // Add or remove the address from the interface.
  // TODO(https://fxbug.dev/92768): Migrate to fuchsia.net.interfaces.admin/Control.
  std::optional<fuchsia::net::stack::Error> error;
  if (add) {
    fuchsia::net::stack::Stack_AddInterfaceAddressDeprecated_Result result;
    status = net_stack_sync_ptr->AddInterfaceAddressDeprecated(interface_id.value(),
                                                               std::move(subnet), &result);
    if (status == ZX_OK && result.is_err()) {
      error = result.err();
    }
  } else {
    fuchsia::net::stack::Stack_DelInterfaceAddressDeprecated_Result result;
    status = net_stack_sync_ptr->DelInterfaceAddressDeprecated(interface_id.value(),
                                                               std::move(subnet), &result);
    if (status == ZX_OK && result.is_err()) {
      error = result.err();
    }
  }
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to configure interface address to interface id "
                   << interface_id.value() << ": " << zx_status_get_string(status);
    return kPlatformResultFailure;
  }

  // Verify that the result from netstack confirms that the address was actually
  // added or removed.
  if (error.has_value()) {
    if (add) {
      constexpr std::string_view msg = "Failed to add address on interface id ";
      if (error.value() == fuchsia::net::stack::Error::ALREADY_EXISTS) {
        FX_LOGS(WARNING) << msg << interface_id.value() << ": "
                         << StackErrorToString(error.value());
        return kPlatformResultSuccess;
      }
      FX_LOGS(ERROR) << msg << interface_id.value() << ": " << StackErrorToString(error.value());
    } else {
      constexpr std::string_view msg = "Failed to remove address on interface id ";
      if (error.value() == fuchsia::net::stack::Error::NOT_FOUND) {
        FX_LOGS(WARNING) << msg << interface_id.value() << ": "
                         << StackErrorToString(error.value());
        return kPlatformResultSuccess;
      }
      FX_LOGS(ERROR) << msg << interface_id.value() << ": " << StackErrorToString(error.value());
    }
    return kPlatformResultFailure;
  }

  FX_LOGS(INFO) << (add ? "Added" : "Removed") << " address from interface id "
                << interface_id.value();

  // If this is not a Thread interface, adding the host address is sufficient.
  // Otherwise, move onto register the on-mesh prefix.
  if (interface_type != InterfaceType::kInterfaceTypeThread) {
    return kPlatformResultSuccess;
  }

  fuchsia::lowpan::device::LookupSyncPtr device_lookup;
  fuchsia::lowpan::device::Lookup_LookupDevice_Result device_lookup_result;
  fuchsia::lowpan::device::Protocols device_protocols;
  fuchsia::lowpan::device::DeviceRouteSyncPtr route_sync_ptr;

  status = svc->Connect(device_lookup.NewRequest());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to connect to lowpan service: " << zx_status_get_string(status);
    return kPlatformResultFailure;
  }

  device_protocols.set_device_route(route_sync_ptr.NewRequest());
  status = device_lookup->LookupDevice(interface_name.value(), std::move(device_protocols),
                                       &device_lookup_result);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to lookup device: " << zx_status_get_string(status);
    return kPlatformResultFailure;
  } else if (device_lookup_result.is_err()) {
    FX_LOGS(ERROR) << "Failed during lookup: " << static_cast<uint32_t>(device_lookup_result.err());
    return kPlatformResultFailure;
  }

  fuchsia::lowpan::Ipv6Subnet mesh_prefix_subnet;
  std::memcpy(mesh_prefix_subnet.addr.addr.data(), (uint8_t *)(address.Addr),
              mesh_prefix_subnet.addr.addr.size());
  mesh_prefix_subnet.prefix_len = prefix_length;

  fuchsia::lowpan::device::OnMeshPrefix mesh_prefix;
  mesh_prefix.set_subnet(mesh_prefix_subnet);
  mesh_prefix.set_default_route_preference(fuchsia::lowpan::device::RoutePreference::MEDIUM);
  mesh_prefix.set_stable(true);
  mesh_prefix.set_slaac_preferred(true);
  mesh_prefix.set_slaac_valid(true);

  status = add ? route_sync_ptr->RegisterOnMeshPrefix(std::move(mesh_prefix))
               : route_sync_ptr->UnregisterOnMeshPrefix(mesh_prefix.subnet());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to " << (add ? "register" : "unregister") << " on-mesh prefix.";
    return kPlatformResultFailure;
  }

  FX_LOGS(INFO) << (add ? "Registered" : "Unregistered") << " on-mesh prefix for Thread.";
  return kPlatformResultSuccess;
}

// Add or remove route to/from forwarding table.
PlatformResult AddRemoveRouteInternal(InterfaceType interface_type, const Inet::IPPrefix &prefix,
                                      RoutePriority priority, bool add) {
  auto svc = nl::Weave::DeviceLayer::PlatformMgrImpl().GetComponentContextForProcess()->svc();

  // Determine interface name to add to/remove from.
  std::optional<std::string> interface_name = GetInterfaceName(interface_type);
  if (!interface_name) {
    return kPlatformResultFailure;
  }

  fuchsia::net::stack::StackSyncPtr net_stack_sync_ptr;
  if (zx_status_t status = svc->Connect(net_stack_sync_ptr.NewRequest()); status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to connect to netstack: " << zx_status_get_string(status);
    return kPlatformResultFailure;
  }

  // Determine the interface ID to add/remove from.
  std::optional<uint64_t> interface_id = GetInterfaceId(interface_name.value());
  if (!add && interface_id && interface_id.value() == 0) {
    // When removing, don't report an error if the interface wasn't found. The
    // interface may already have been removed at this point.
    FX_LOGS(INFO) << "Interface " << interface_name.value() << " has already been removed.";
    return kPlatformResultSuccess;
  } else if (!interface_id) {
    return kPlatformResultFailure;
  }

  // Construct route table entry to add or remove.
  fuchsia::net::stack::ForwardingEntry route_table_entry{
      .subnet =
          {
              .addr = fuchsia::net::IpAddress::WithIpv6([&prefix]() {
                fuchsia::net::Ipv6Address ipv6_addr;
                std::memcpy(ipv6_addr.addr.data(), (uint8_t *)(prefix.IPAddr.Addr),
                            ipv6_addr.addr.size());
                return ipv6_addr;
              }()),
              .prefix_len = prefix.Length,
          },
      .device_id = interface_id.value(),
  };
  switch (priority) {
    case RoutePriority::kRoutePriorityHigh:
      route_table_entry.metric = kRouteMetric_HighPriority;
      break;
    case RoutePriority::kRoutePriorityMedium:
      route_table_entry.metric = kRouteMetric_MediumPriority;
      break;
    case RoutePriority::kRoutePriorityLow:
      route_table_entry.metric = kRouteMetric_LowPriority;
      break;
    default:
      FX_LOGS(WARNING) << "Unhandled route priority type, using lowest priority.";
      route_table_entry.metric = kRouteMetric_LowPriority;
  }

  std::optional<fuchsia::net::stack::Error> error;
  if (add) {
    fuchsia::net::stack::Stack_AddForwardingEntry_Result result;
    if (zx_status_t status =
            net_stack_sync_ptr->AddForwardingEntry(std::move(route_table_entry), &result);
        status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to add route";
      return kPlatformResultFailure;
    }
    if (result.is_err()) {
      error = result.err();
    }
  } else {
    fuchsia::net::stack::Stack_DelForwardingEntry_Result result;
    if (zx_status_t status =
            net_stack_sync_ptr->DelForwardingEntry(std::move(route_table_entry), &result);
        status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to delete route";
      return kPlatformResultFailure;
    }
    if (result.is_err()) {
      error = result.err();
    }
  }
  if (error.has_value()) {
    FX_LOGS(ERROR) << "Unable to modify route: " << StackErrorToString(error.value());
    return kPlatformResultFailure;
  }

  FX_LOGS(INFO) << (add ? "Added" : "Removed") << " route to/from interface id "
                << interface_id.value();

#if WARM_CONFIG_SUPPORT_BORDER_ROUTING
  // Set IPv6 forwarding on interface. Note that IPv6 forwarding is only ever
  // enabled and never disabled. Once an interface is being managed, routes may
  // be added and removed over time, so do not thrash the forwarding state
  // during these transitions.
  //
  // TODO(https://fxbug.dev/78254): Enabling V6 forwarding should ideally happen
  // elsewhere, as this function's contract does not make any specific mention about
  // forwarding. However, implementations must enable forwarding when border routing
  // is enabled, signaled by ThreadThreadRouteAction and ThreadThreadPriorityAction
  // in WARM. As we defer those route changes to this function, this is done here for
  // now. Long term, this bug tracks proposing upstream changes that would create a
  // targeted action to enable forwarding / border-routing to clarify this contract.
  if (add && ShouldEnableV6Forwarding(interface_type)) {
    fuchsia::net::stack::Stack_SetInterfaceIpForwarding_Result forwarding_result;
    if (zx_status_t status = net_stack_sync_ptr->SetInterfaceIpForwarding(
            interface_id.value(), fuchsia::net::IpVersion::V6, true /* enable */,
            &forwarding_result);
        status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to enable IPv6 forwarding on interface id "
                              << interface_id.value();
      return kPlatformResultFailure;
    } else if (forwarding_result.is_err()) {
      FX_LOGS(ERROR) << "Unable to enable IPv6 forwarding on interface id " << interface_id.value()
                     << ": " << static_cast<uint32_t>(forwarding_result.err());
      return kPlatformResultFailure;
    }
  }
#endif  // WARM_CONFIG_SUPPORT_BORDER_ROUTING

  return kPlatformResultSuccess;
}

}  // namespace

WEAVE_ERROR Init(WarmFabricStateDelegate *inFabricStateDelegate) { return WEAVE_NO_ERROR; }

NL_DLL_EXPORT
void CriticalSectionEnter(void) {}

NL_DLL_EXPORT
void CriticalSectionExit(void) {}

// Add or remove a host address attached to the Thread or WLAN interfaces.
PlatformResult AddRemoveHostAddress(InterfaceType interface_type, const Inet::IPAddress &address,
                                    uint8_t prefix_length, bool add) {
  return AddRemoveAddressInternal(interface_type, address, prefix_length, add);
}

// Add or remove a host route attached to the Thread or WLAN interfaces.
PlatformResult AddRemoveHostRoute(InterfaceType interface_type, const Inet::IPPrefix &prefix,
                                  RoutePriority priority, bool add) {
  return AddRemoveRouteInternal(interface_type, prefix, priority, add);
}

NL_DLL_EXPORT
void RequestInvokeActions(void) { ::nl::Weave::Warm::InvokeActions(); }

#if WARM_CONFIG_SUPPORT_THREAD
PlatformResult AddRemoveThreadAddress(InterfaceType interface_type, const Inet::IPAddress &address,
                                      bool add) {
  // Prefix length for Thread addresses.
  static constexpr uint8_t kThreadPrefixLength = 64;
  return AddRemoveAddressInternal(interface_type, address, kThreadPrefixLength, add);
}
#endif  // WARM_CONFIG_SUPPORT_THREAD

#if WARM_CONFIG_SUPPORT_THREAD_ROUTING
PlatformResult StartStopThreadAdvertisement(InterfaceType interface_type,
                                            const Inet::IPPrefix &prefix, bool start) {
  // This is handled by the LoWPAN service, nothing to do here.
  return kPlatformResultSuccess;
}
#endif  // WARM_CONFIG_SUPPORT_THREAD_ROUTING

#if WARM_CONFIG_SUPPORT_BORDER_ROUTING
PlatformResult AddRemoveThreadRoute(InterfaceType interface_type, const Inet::IPPrefix &prefix,
                                    RoutePriority priority, bool add) {
  return AddRemoveRouteInternal(interface_type, prefix, priority, add);
}

PlatformResult SetThreadRoutePriority(InterfaceType interface_type, const Inet::IPPrefix &prefix,
                                      RoutePriority priority) {
  // This will be handled during the AddRemoveThreadRoute from WARM.
  return kPlatformResultSuccess;
}
#endif  // WARM_CONFIG_SUPPORT_BORDER_ROUTING

}  // namespace Platform
}  // namespace Warm
}  // namespace Weave
}  // namespace nl
