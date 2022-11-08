// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#include <Weave/DeviceLayer/ConnectivityManager.h>
#include <Weave/DeviceLayer/ThreadStackManager.h>
#include <Warm/Warm.h>
// clang-format on

#include <fidl/fuchsia.net.debug/cpp/fidl.h>
#include <fidl/fuchsia.net.interfaces.admin/cpp/fidl.h>
#include <fuchsia/net/cpp/fidl.h>
#include <fuchsia/net/interfaces/cpp/fidl.h>
#include <fuchsia/net/stack/cpp/fidl.h>
#include <fuchsia/netstack/cpp/fidl.h>
#include <lib/fidl/cpp/wire/channel.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/syslog/cpp/macros.h>
#include <netinet/ip6.h>

#include <optional>

// ==================== WARM Platform Functions ====================

namespace nl::Weave::Warm::Platform {

namespace {
using DeviceLayer::ConnectivityMgrImpl;

constexpr char kFuchsiaNetDebugInterfacesProtocolName[] =
    "fuchsia.net.debug.Interfaces_OnlyForWeavestack";

// Fixed name for tunnel interface.
constexpr char kTunInterfaceName[] = "weav-tun0";

// Route metric values for primary and backup tunnels. Higher priority tunnels
// have lower metric values so that they are prioritized in the routing table.
constexpr uint32_t kRouteMetric_HighPriority = 0;
constexpr uint32_t kRouteMetric_MediumPriority = 99;
constexpr uint32_t kRouteMetric_LowPriority = 999;

std::optional<std::string> AddressToString(const Inet::IPAddress &address) {
  const uint8_t IPV6ADDR_STRLEN_MAX = 46;
  char buf[IPV6ADDR_STRLEN_MAX];
  if (address.ToString(buf, IPV6ADDR_STRLEN_MAX)) {
    return std::string(buf);
  } else {
    return std::nullopt;
  }
}

std::string AddressToStringInfallible(const Inet::IPAddress &address) {
  return AddressToString(address).value_or("<UNFORMATTABLE ADDRESS>");
}

class AddressStateProviderRepository {
 public:
  // Add the `AddressStateProvider` handle to the repository. If an entry
  // already exists, it will be overwritten.
  PlatformResult Add(
      uint64_t interface_id, const Inet::IPAddress &address,
      fidl::SyncClient<fuchsia_net_interfaces_admin::AddressStateProvider> asp_client) {
    std::optional<std::string> key = GetKey(interface_id, address);
    if (!key.has_value()) {
      FX_LOGS(ERROR) << "Could not generate a key for address " << AddressToStringInfallible
                     << " on interface " << interface_id;
      return kPlatformResultFailure;
    }
    if (repo_.find(*key) != repo_.end()) {
      FX_LOGS(WARNING) << "Overwritting existing AddressStateProvider client stored for address "
                       << AddressToStringInfallible(address) << " on interface " << interface_id;
    }
    repo_[*key] =
        std::make_unique<fidl::SyncClient<fuchsia_net_interfaces_admin::AddressStateProvider>>(
            std::move(asp_client));
    return kPlatformResultSuccess;
  }

  // Erase the `AddressStateProvider` handle from the repository.
  // Returns true if the handle was present, false otherwise.
  // Note that erase causes the handle to be dropped, which triggers removal
  // of the address from the Netstack, if not detached.
  bool Erase(uint64_t interface_id, const Inet::IPAddress &address) {
    std::optional<std::string> key = GetKey(interface_id, address);
    if (!key.has_value()) {
      FX_LOGS(ERROR) << "Could not generate a key for address " << AddressToStringInfallible
                     << " on interface " << interface_id;
      return false;
    }
    return repo_.erase(*key);
  }

 private:
  // Deterministically converts the given interface_id and address into a unique
  // key for the local storage.
  // Returns std::nullopt if the address could not be formatted into a key.
  std::optional<std::string> GetKey(uint64_t interface_id, const Inet::IPAddress &address) {
    std::optional<std::string> addr_str = AddressToString(address);
    if (addr_str.has_value()) {
      return std::to_string(interface_id) + ":" + *addr_str;
    } else {
      return std::nullopt;
    }
  }

  // The underlying storage of `AddressStateProvider` handles. Note that the
  // `fidl::SyncClient` is wrapped in a unique_ptr, because it cannot be
  // assigned to.
  std::map<std::string,
           std::unique_ptr<fidl::SyncClient<fuchsia_net_interfaces_admin::AddressStateProvider>>>
      repo_;
};

// A Global Store of `AddressStateProvider` client-side handles for all
// addresses installed by Weavestack. The `AddressStateProvider` protocol
// expresses strong ownership; storing them here prevents the handles from being
// dropped and consequently prevents the Netstack from removing the addresses.
AddressStateProviderRepository global_asp_repo;

// Returns the interface name associated with the given interface type.
// Unsupported interface types will not populate the optional.
std::optional<std::string> GetInterfaceName(InterfaceType interface_type) {
  switch (interface_type) {
#if WARM_CONFIG_SUPPORT_THREAD
    case kInterfaceTypeThread:
      return ThreadStackMgrImpl().GetInterfaceName();
#endif  // WARM_CONFIG_SUPPORT_THREAD
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
  return ((DeviceLayer::ConfigurationMgrImpl().IsIPv6ForwardingEnabled()) &&
          ((interface_type == kInterfaceTypeThread) || (interface_type == kInterfaceTypeTunnel)));
}

// Returns the interface id associated with the given interface name. On
// failures to fetch the list, no value will be returned. When the interface
// does not exist, the interface ID '0' will be returned (it is guaranteed by
// the networking stack that all valid interface IDs are positive).
std::optional<uint64_t> GetInterfaceId(const std::string &interface_name) {
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

std::string_view AddressRemovalReasonToString(
    fuchsia_net_interfaces_admin::AddressRemovalReason reason) {
  switch (reason) {
    case fuchsia_net_interfaces_admin::AddressRemovalReason::kInvalid:
      return "invalid";
    case fuchsia_net_interfaces_admin::AddressRemovalReason::kAlreadyAssigned:
      return "already assigned";
    case fuchsia_net_interfaces_admin::AddressRemovalReason::kDadFailed:
      return "DAD failed";
    case fuchsia_net_interfaces_admin::AddressRemovalReason::kInterfaceRemoved:
      return "interface removed";
    case fuchsia_net_interfaces_admin::AddressRemovalReason::kUserRemoved:
      return "user removed";
  }
}

std::string_view AddressAssignmentStateToString(
    fuchsia_net_interfaces_admin::AddressAssignmentState state) {
  switch (state) {
    case fuchsia_net_interfaces_admin::AddressAssignmentState::kTentative:
      return "tentative";
    case fuchsia_net_interfaces_admin::AddressAssignmentState::kAssigned:
      return "assigned";
    case fuchsia_net_interfaces_admin::AddressAssignmentState::kUnavailable:
      return "unavailable";
  }
}

// Retrieve a handle to the `fuchsia.net.interfaces.admin/Control' API.
//
// Note that this uses `fuchsia.net.debug/Interfaces.GetAdmin` to do so, which
// circumvents the strong ownership model of the `fuchsia.net.interfaces.api`.
// This pattern is discouraged, but approved for this use case in Weavestack,
// see (https://bugs.fuchsia.dev/p/fuchsia/issues/detail?id=92768#c6).
// TODO(https://fxbug.dev/111695) Delete the usage of the debug API once an
// alternative API is available.
std::optional<fidl::SyncClient<fuchsia_net_interfaces_admin::Control>> GetInterfaceControlViaDebug(
    uint64_t interface_id) {
  auto svc = nl::Weave::DeviceLayer::PlatformMgrImpl().GetComponentContextForProcess()->svc();

  zx::result debug_endpoints = fidl::CreateEndpoints<fuchsia_net_debug::Interfaces>();
  if (!debug_endpoints.is_ok()) {
    FX_LOGS(ERROR)
        << "Synchronous Error while connecting to the |fuchsia.net.debug/Interfaces| protocol: "
        << debug_endpoints.status_string();
    return std::nullopt;
  }
  auto [debug_client_end, debug_server_end] = std::move(*debug_endpoints);
  if (zx_status_t status =
          svc->Connect(kFuchsiaNetDebugInterfacesProtocolName, debug_server_end.TakeChannel());
      status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to connect to debug: " << zx_status_get_string(status);
    return std::nullopt;
  }
  fidl::SyncClient debug_client{std::move(debug_client_end)};

  zx::result control_endpoints = fidl::CreateEndpoints<fuchsia_net_interfaces_admin::Control>();
  if (!control_endpoints.is_ok()) {
    FX_LOGS(ERROR) << "Synchronous error when connecting to the "
                      "|fuchsia.net.interfaces.admin/Control| protocol: "
                   << control_endpoints.status_string();
    return std::nullopt;
  }
  auto [control_client_end, control_server_end] = std::move(*control_endpoints);
  auto result = debug_client->GetAdmin({interface_id, std::move(control_server_end)});
  if (!result.is_ok()) {
    FX_LOGS(ERROR) << "Failure while invoking |GetAdmin| FIDL method: " << result.error_value();
    return std::nullopt;
  }
  return fidl::SyncClient(std::move(control_client_end));
}

// A client of the `fuchsia.net.interfaces.admin/Control` protocol that is
// somewhat agnostic to FIDL bindings (e.g. HLCPP vs LLCPP).
//
// When Constructed, it takes the `zx::channel` from the given `ControlSyncPtr`
// (HLCPP) and uses it to create a `fidl::SyncClient` (LLCPP).
//
// When Destructed, it takes the `zx::channel` from the `fidl::SyncClient`
// (LLCPP), and rebinds it to the original `ControlSyncPtr` (HLCPP).
class LlcppControlClientFromHlcpp {
 public:
  // Constructor takes the `zx::channel` from the hlcpp_client.
  LlcppControlClientFromHlcpp(fuchsia::net::interfaces::admin::ControlSyncPtr *hlcpp_client) {
    hlcpp_client_ = hlcpp_client;
    llcpp_client_ = fidl::SyncClient<fuchsia_net_interfaces_admin::Control>(
        fidl::ClientEnd<fuchsia_net_interfaces_admin::Control>(
            hlcpp_client_->Unbind().TakeChannel()));
  }

  // Destructor returns the `zx::channel` to the hlcpp_client.
  ~LlcppControlClientFromHlcpp() {
    hlcpp_client_->Bind(llcpp_client_.TakeClientEnd().TakeChannel());
  }

  const fidl::SyncClient<fuchsia_net_interfaces_admin::Control> &Get() { return llcpp_client_; }

 private:
  // The HLCPP client whose `zx::channel` is taken when this object is
  // constructed. The `zx::channel` is rebound to the HLCPP client when this
  // object is destructed.
  fuchsia::net::interfaces::admin::ControlSyncPtr *hlcpp_client_;
  // The LLCPP client that holds the `zx::channel` throughout the lifetime of
  // this object.
  fidl::SyncClient<fuchsia_net_interfaces_admin::Control> llcpp_client_;
};

// A Handler for
// `fuchsia.net.interfaces.admin/AddressStateProvider.OnAddressRemoved` events
// that simply records the `AddressRemovalReason`.
class OnAddressRemovedHandler
    : public fidl::SyncEventHandler<fuchsia_net_interfaces_admin::AddressStateProvider> {
 public:
  void OnAddressRemoved(
      fidl::Event<fuchsia_net_interfaces_admin::AddressStateProvider::OnAddressRemoved> &event)
      override {
    removal_reason = event.error();
  }

  fuchsia_net_interfaces_admin::AddressRemovalReason removal_reason;
};

// Adds the given address to the given interface.
//
// This function waits for the given address to be assigned, and then detaches
// the AddressStateProvider FIDL client from the address lifetime in the
// Netstack.
//
// If the address already exists, `kPlatformResultSuccess` will be returned.
PlatformResult AddAddressInternal(
    uint64_t interface_id, const Inet::IPAddress &address, uint8_t prefix_length,
    const fidl::SyncClient<fuchsia_net_interfaces_admin::Control> &control_client) {
  FX_LOGS(INFO) << "Adding address " << AddressToStringInfallible(address)
                << " to interface with id " << interface_id;

  // Construct the IP address for the interface.
  std::array<uint8_t, 16> ipv6_addr_bytes;
  std::memcpy(ipv6_addr_bytes.data(), reinterpret_cast<const uint8_t *>(address.Addr),
              ipv6_addr_bytes.size());
  fuchsia_net::Ipv6Address ipv6_addr = fuchsia_net::Ipv6Address(ipv6_addr_bytes);
  fuchsia_net::IpAddress ip_addr = fuchsia_net::IpAddress::WithIpv6(ipv6_addr);
  fuchsia_net::Subnet subnet = fuchsia_net::Subnet(std::move(ip_addr), prefix_length);

  // Empty `AddressParameters`.
  fuchsia_net_interfaces_admin::AddressParameters params;
  // FIDL Handle for the `AddressStateProvider` protocol.
  zx::result asp_endpoints =
      fidl::CreateEndpoints<fuchsia_net_interfaces_admin::AddressStateProvider>();
  if (!asp_endpoints.is_ok()) {
    FX_LOGS(ERROR) << "Synchronous error when connecting to the AddressStateProvider protocol: "
                   << asp_endpoints.status_string();
    return kPlatformResultFailure;
  }
  auto [asp_client_end, asp_server_end] = std::move(*asp_endpoints);

  auto result =
      control_client->AddAddress({std::move(subnet), std::move(params), std::move(asp_server_end)});
  if (!result.is_ok()) {
    FX_LOGS(ERROR) << "Failed to add address " << AddressToStringInfallible(address)
                   << " to interface with id " << interface_id << ": " << result.error_value();
    return kPlatformResultFailure;
  }

  // Adding an address is asynchronous: After successfully calling "AddAddress"
  // we must wait either for the AddressStateProvider protocol to be closed
  // (indicating an error) or for the Address to become `ASSIGNED`.
  FX_LOGS(INFO) << "Waiting for address to be assigned...";
  fidl::SyncClient asp_client{std::move(asp_client_end)};
  fuchsia_net_interfaces_admin::AddressAssignmentState state;
  do {
    fidl::Result result = asp_client->WatchAddressAssignmentState();
    if (!result.is_ok()) {
      // Non Peer Closed errors are unexpected.
      if (!result.error_value().is_peer_closed()) {
        FX_LOGS(ERROR) << "Failure while invoking |WatchAddressAssignmentState|: "
                       << result.error_value();
        return kPlatformResultFailure;
      }
      // Peer Closed errors will be accompanied by an `OnAddressRemoved` event.
      // That provides additional context as to why the error was observed.
      OnAddressRemovedHandler handler;
      fidl::Status status = handler.HandleOneEvent(asp_client.client_end());
      if (!status.ok()) {
        FX_LOGS(ERROR) << "Failure while handling |OnAddressRemoved|: " << status.status_string();
        return kPlatformResultFailure;
      }
      if (handler.removal_reason ==
          fuchsia_net_interfaces_admin::AddressRemovalReason::kAlreadyAssigned) {
        FX_LOGS(WARNING) << "Address " << AddressToStringInfallible(address)
                         << " was already assigned to interface " << interface_id;
        return kPlatformResultSuccess;
      } else {
        FX_LOGS(ERROR) << "Failed to add address " << AddressToStringInfallible(address)
                       << " to Interface " << interface_id << " : "
                       << AddressRemovalReasonToString(handler.removal_reason);
        return kPlatformResultFailure;
      }
    }
    state = result.value().assignment_state();
    FX_LOGS(INFO) << "Observed state change for " << AddressToStringInfallible(address) << ": "
                  << AddressAssignmentStateToString(state);
  } while (state != fuchsia_net_interfaces_admin::AddressAssignmentState::kAssigned);

  PlatformResult store_handle_result =
      global_asp_repo.Add(interface_id, address, std::move(asp_client));
  if (store_handle_result == kPlatformResultFailure) {
    FX_LOGS(ERROR) << "Failed to Store AddressStateProvider handle for address "
                   << AddressToStringInfallible(address) << " on interface with id "
                   << interface_id;
  } else {
    FX_LOGS(INFO) << "Successfully added address " << AddressToStringInfallible(address)
                  << " to interface with id " << interface_id;
  }
  return store_handle_result;
}

// Removes the given address from the given interface.
//
// If the address was not found, `kPlatformResultSuccess` will be returned.
PlatformResult RemoveAddressInternal(uint64_t interface_id, const Inet::IPAddress &address) {
  FX_LOGS(INFO) << "Removing address " << AddressToStringInfallible(address)
                << " from interface with id " << interface_id;
  // Erase the ASP client-side handle from the global repository. Note that when
  // the handle is dropped, the Netstack will remove the address.
  if (!global_asp_repo.Erase(interface_id, address)) {
    FX_LOGS(WARNING) << "Did not remove non-existant address " << AddressToStringInfallible(address)
                     << " from interface with id " << interface_id;
  } else {
    FX_LOGS(INFO) << "Successfully removed address " << AddressToStringInfallible(address)
                  << " from interface with id " << interface_id;
  }
  return kPlatformResultSuccess;
}

PlatformResult AddWiFiAddress(uint64_t interface_id, const Inet::IPAddress &address,
                              uint8_t prefix_length) {
  std::optional<fidl::SyncClient<fuchsia_net_interfaces_admin::Control>> control_client =
      GetInterfaceControlViaDebug(interface_id);
  if (!control_client) {
    FX_LOGS(ERROR)
        << "Failed to acquire |fuchsia.net.interfaces.admin/Control| handle for interface "
        << interface_id;
    return kPlatformResultFailure;
  }
  return AddAddressInternal(interface_id, address, prefix_length, control_client.value());
}

PlatformResult AddTunnelAddress(uint64_t interface_id, const Inet::IPAddress &address,
                                uint8_t prefix_length) {
  fuchsia::net::interfaces::admin::ControlSyncPtr *hlcpp_control_client =
      ConnectivityMgrImpl().GetTunInterfaceControlSyncPtr();
  if (hlcpp_control_client == nullptr || !hlcpp_control_client->is_bound()) {
    FX_LOGS(ERROR) << "Tun Interface does not have an owned Control handle.";
    return kPlatformResultFailure;
  }
  // When dropped, returns the `zx_channel` to `hlcpp_control_client`.
  LlcppControlClientFromHlcpp llcpp_control_client =
      LlcppControlClientFromHlcpp(hlcpp_control_client);
  return AddAddressInternal(interface_id, address, prefix_length, llcpp_control_client.Get());
}

// Add or remove an address attached to the Thread or WLAN interfaces.
PlatformResult AddRemoveAddressInternal(InterfaceType interface_type,
                                        const Inet::IPAddress &address, uint8_t prefix_length,
                                        bool add) {
  // Determine interface name to add/remove from.
  std::optional<std::string> interface_name = GetInterfaceName(interface_type);
  if (!interface_name) {
    return kPlatformResultFailure;
  }

  // Determine the interface ID to add/remove from.
  std::optional<uint64_t> interface_id = GetInterfaceId(interface_name.value());
  if (!add && interface_id && interface_id.value() == 0) {
    // When removing, don't report an error if the interface wasn't found. The
    // interface may already have been removed at this point.
    FX_LOGS(INFO) << "Interface " << interface_name.value() << " has already been removed.";
    return kPlatformResultSuccess;
  }
  if (!interface_id) {
    return kPlatformResultFailure;
  }

  if (add) {
    switch (interface_type) {
      case kInterfaceTypeTunnel:
        return AddTunnelAddress(interface_id.value(), address, prefix_length);
      case kInterfaceTypeWiFi:
        return AddWiFiAddress(interface_id.value(), address, prefix_length);
      default:
        FX_LOGS(ERROR) << "Unsupported interface type: " << interface_type;
        return kPlatformResultFailure;
    }
  } else {
    return RemoveAddressInternal(interface_id.value(), address);
  }
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
  }
  if (!interface_id) {
    return kPlatformResultFailure;
  }

  // Construct route table entry to add or remove.
  fuchsia::net::stack::ForwardingEntry route_table_entry{
      .subnet =
          {
              .addr = fuchsia::net::IpAddress::WithIpv6([&prefix]() {
                fuchsia::net::Ipv6Address ipv6_addr;
                std::memcpy(ipv6_addr.addr.data(),
                            reinterpret_cast<const uint8_t *>(prefix.IPAddr.Addr),
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
    // TODO(https://fxbug.dev/94540): Migrate to
    // fuchsia.net.interfaces.admin/Control.SetConfiguration.
    fuchsia::net::stack::Stack_SetInterfaceIpForwardingDeprecated_Result forwarding_result;
    if (zx_status_t status = net_stack_sync_ptr->SetInterfaceIpForwardingDeprecated(
            interface_id.value(), fuchsia::net::IpVersion::V6, true /* enable */,
            &forwarding_result);
        status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to enable IPv6 forwarding on interface id "
                              << interface_id.value();
      return kPlatformResultFailure;
    }
    if (forwarding_result.is_err()) {
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
void CriticalSectionEnter() {}

NL_DLL_EXPORT
void CriticalSectionExit() {}

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
void RequestInvokeActions() { ::nl::Weave::Warm::InvokeActions(); }

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

}  // namespace nl::Weave::Warm::Platform
