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
#include <fuchsia/net/stack/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <netinet/ip6.h>

// ==================== WARM Platform Functions ====================

namespace nl {
namespace Weave {
namespace Warm {
namespace Platform {

namespace {
using namespace ::nl::Weave::DeviceLayer;
using namespace ::nl::Weave::DeviceLayer::Internal;
using namespace ::nl;
using namespace ::nl::Weave;
using namespace ::nl::Weave::Warm;

constexpr char kTunInterfaceName[] = "weav-tun0";
constexpr uint8_t kSubnetPrefixLen = 48;

// Get the interface name associated with the interface type. Returns true on success or false if
// the type is not yet supported.
bool GetInterfaceName(InterfaceType interface_type, std::string *interface_name) {
  switch (interface_type) {
    case kInterfaceTypeThread:
      *interface_name = ThreadStackMgrImpl().GetInterfaceName();
      return true;
    case kInterfaceTypeTunnel:
      *interface_name = kTunInterfaceName;
      return true;
    default:
      return false;
  }
}

// Get network interface id.
zx_status_t GetInterface(fuchsia::net::stack::StackSyncPtr &stack_sync_ptr,
                         std::string interface_name, uint64_t *interface_id) {
  std::vector<fuchsia::net::stack::InterfaceInfo> ifs;

  if (interface_id == NULL) {
    FX_LOGS(ERROR) << "interface_id is NULL.";
    return ZX_ERR_INVALID_ARGS;
  }

  zx_status_t status = stack_sync_ptr->ListInterfaces(&ifs);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "ListInterfaces failed: " << zx_status_get_string(status);
    return status;
  }

  std::vector<fuchsia::net::stack::InterfaceInfo>::iterator it;
  it = std::find_if(ifs.begin(), ifs.end(), [&](const fuchsia::net::stack::InterfaceInfo &info) {
    return info.properties.name == interface_name;
  });

  if (it == ifs.end()) {
    FX_LOGS(ERROR) << "Unable to find interface \"" << interface_name << "\".";
    return ZX_ERR_NOT_FOUND;
  }

  *interface_id = it->id;
  return ZX_OK;
}

}  // namespace

WEAVE_ERROR Init(WarmFabricStateDelegate *inFabricStateDelegate) { return WEAVE_NO_ERROR; }

void CriticalSectionEnter(void) {}

void CriticalSectionExit(void) {}

void RequestInvokeActions(void) { ::nl::Weave::Warm::InvokeActions(); }

// Add or remove address on tunnel interface.
PlatformResult AddRemoveHostAddress(InterfaceType interface_type, const Inet::IPAddress &address,
                                    uint8_t prefix_length, bool add) {
  fuchsia::net::stack::StackSyncPtr stack_sync_ptr;
  fuchsia::net::IpAddress addr;
  fuchsia::net::Ipv6Address v6;
  uint64_t interface_id = 0;
  fuchsia::net::Subnet ifaddr;
  auto svc = nl::Weave::DeviceLayer::PlatformMgrImpl().GetComponentContextForProcess()->svc();

  // Determine interface name to add to/remove from.
  std::string interface_name;
  if (!GetInterfaceName(interface_type, &interface_name)) {
    FX_LOGS(ERROR) << "Cannot handle interface type \"" << interface_type << "\".";
    return kPlatformResultFailure;
  }

  // Connect to the net Stack and grab the interface ID requested.
  zx_status_t status = svc->Connect(stack_sync_ptr.NewRequest());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Connect to netstack failed: " << status;
    return kPlatformResultFailure;
  }
  status = GetInterface(stack_sync_ptr, interface_name, &interface_id);
  if (status != ZX_OK) {
    return kPlatformResultFailure;
  }

  // Set up the ip address and prefix.
  std::memcpy(v6.addr.data(), (uint8_t *)(address.Addr), v6.addr.size());
  addr.set_ipv6(v6);
  ifaddr.addr = std::move(addr);
  ifaddr.prefix_len = prefix_length;

  if (add) {
    // Add the address to the interface.
    fuchsia::net::stack::Stack_AddInterfaceAddress_Result result;
    status = stack_sync_ptr->AddInterfaceAddress(interface_id, std::move(ifaddr), &result);
    if (status != ZX_OK || result.is_err()) {
      FX_LOGS(ERROR) << "Failed to add interface address to id: " << interface_id
                     << " status: " << zx_status_get_string(status);
      return kPlatformResultFailure;
    }
  } else {
    // Remove the address from the interface.
    fuchsia::net::stack::Stack_DelInterfaceAddress_Result result;
    status = stack_sync_ptr->DelInterfaceAddress(interface_id, std::move(ifaddr), &result);
    if (status != ZX_OK || result.is_err()) {
      FX_LOGS(ERROR) << "Failed to delete interface address for id: " << interface_id
                     << " status: " << zx_status_get_string(status);
      return kPlatformResultFailure;
    }
  }

  FX_LOGS(INFO) << "AddRemoveHostAddress successful.";

  return kPlatformResultSuccess;
}

// Add or remove route to/from forwarding table.
PlatformResult AddRemoveHostRoute(InterfaceType interface_type, const Inet::IPPrefix &prefix,
                                  RoutePriority priority, bool add) {
  uint64_t interface_id = 0;
  fuchsia::net::stack::StackSyncPtr stack_sync_ptr;
  fuchsia::net::Ipv6Address v6;
  fuchsia::net::stack::ForwardingEntry entry;
  auto svc = nl::Weave::DeviceLayer::PlatformMgrImpl().GetComponentContextForProcess()->svc();

  // Determine interface name to add to/remove from.
  std::string interface_name;
  if (!GetInterfaceName(interface_type, &interface_name)) {
    FX_LOGS(ERROR) << "Cannot handle interface type \"" << interface_type << "\".";
    return kPlatformResultFailure;
  }

  // Connect to the net Stack and grab the interface ID requested.
  zx_status_t status = svc->Connect(stack_sync_ptr.NewRequest());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Connect to netstack failed: " << status;
    return kPlatformResultFailure;
  }
  status = GetInterface(stack_sync_ptr, interface_name, &interface_id);
  if (status != ZX_OK) {
    return kPlatformResultSuccess;
  }

  std::memcpy(v6.addr.data(), (uint8_t *)(prefix.IPAddr.Addr), v6.addr.size());
  if (add) {
    // Add the forwarding entry for the specified interface.
    fuchsia::net::stack::Stack_AddForwardingEntry_Result result;
    entry.subnet.addr.set_ipv6(v6);
    entry.subnet.prefix_len = kSubnetPrefixLen;
    entry.destination.set_device_id(interface_id);
    status = stack_sync_ptr->AddForwardingEntry(std::move(entry), &result);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "AddForwardingEntry failed: " << zx_status_get_string(status);
      return kPlatformResultFailure;
    }

    if (result.is_err()) {
      FX_LOGS(ERROR) << "AddForwardingEntry failed result:";
      return kPlatformResultFailure;
    }
  } else {
    // Remove the forwarding entry for the specified interface.
    fuchsia::net::Subnet subnet;
    subnet.addr.set_ipv6(v6);
    subnet.prefix_len = prefix.Length;
    fuchsia::net::stack::Stack_DelForwardingEntry_Result result;
    status = stack_sync_ptr->DelForwardingEntry(std::move(subnet), &result);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "DelForwardingEntry failed: " << zx_status_get_string(status);
      return kPlatformResultFailure;
    }
    if (result.is_err()) {
      FX_LOGS(ERROR) << "DelForwardingEntry failed result.";
      return kPlatformResultFailure;
    }
  }

  FX_LOGS(INFO) << "AddRemoveHostRoute successful.";
  return kPlatformResultSuccess;
}

#if WARM_CONFIG_SUPPORT_THREAD
PlatformResult AddRemoveThreadAddress(InterfaceType inInterfaceType,
                                      const Inet::IPAddress &inAddress, bool inAdd) {
  // This will be handled during the subsequent AddRemoveHostAddress from WARM.
  return kPlatformResultSuccess;
}
#endif  // WARM_CONFIG_SUPPORT_THREAD

#if WARM_CONFIG_SUPPORT_THREAD_ROUTING
PlatformResult StartStopThreadAdvertisement(InterfaceType inInterfaceType,
                                            const Inet::IPPrefix &inPrefix, bool inStart) {
  // This is handled by the LoWPAN service, nothing to do here.
  return kPlatformResultSuccess;
}
#endif  // WARM_CONFIG_SUPPORT_THREAD_ROUTING

#if WARM_CONFIG_SUPPORT_BORDER_ROUTING
PlatformResult AddRemoveThreadRoute(InterfaceType inInterfaceType, const Inet::IPPrefix &inPrefix,
                                    RoutePriority inPriority, bool inAdd) {
  // This will be handled during the subsequent AddRemoveHostAddress from WARM.
  return kPlatformResultSuccess;
}

PlatformResult SetThreadRoutePriority(InterfaceType inInterfaceType, const Inet::IPPrefix &inPrefix,
                                      RoutePriority inPriority) {
  // Route priority not supported.
  return kPlatformResultSuccess;
}
#endif  // WARM_CONFIG_SUPPORT_BORDER_ROUTING

}  // namespace Platform
}  // namespace Warm
}  // namespace Weave
}  // namespace nl
