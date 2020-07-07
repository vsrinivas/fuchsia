// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#include <Weave/DeviceLayer/ConnectivityManager.h>
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

constexpr char kInterfaceName[] = "weav-tun0";
constexpr uint8_t kSubnetPrefixLen = 48;
}  // namespace

WEAVE_ERROR Init(WarmFabricStateDelegate *inFabricStateDelegate) { return WEAVE_NO_ERROR; }

void CriticalSectionEnter(void) {}

void CriticalSectionExit(void) {}

void RequestInvokeActions(void) { ::nl::Weave::Warm::InvokeActions(); }

// Get tunnel interface id.
zx_status_t GetInterface(fuchsia::net::stack::StackSyncPtr &stack_sync_ptr,
                         uint64_t *interface_id) {
  std::vector<fuchsia::net::stack::InterfaceInfo> ifs;

  if (interface_id == NULL) {
    FX_LOGS(ERROR) << "interface_id is NULL";
    return ZX_ERR_INVALID_ARGS;
  }

  zx_status_t status = stack_sync_ptr->ListInterfaces(&ifs);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "ListInterfaces failed: " << zx_status_get_string(status);
    return status;
  }

  std::vector<fuchsia::net::stack::InterfaceInfo>::iterator it;

  it = std::find_if(ifs.begin(), ifs.end(), [](const fuchsia::net::stack::InterfaceInfo &info) {
    return info.properties.name.compare(kInterfaceName) == 0;
  });

  if (it == ifs.end()) {
    FX_LOGS(ERROR) << "Unable to find the tun interface";
    return ZX_ERR_NOT_FOUND;
  }

  *interface_id = it->id;
  return ZX_OK;
}

// Add or remove address on tunnel interface.
PlatformResult AddRemoveHostAddress(InterfaceType in_interface_type,
                                    const Inet::IPAddress &in_address, uint8_t in_prefix_length,
                                    bool in_add) {
  fuchsia::net::stack::StackSyncPtr stack_sync_ptr;
  fuchsia::net::IpAddress addr;
  fuchsia::net::Ipv6Address v6;
  uint64_t interface_id = 0;
  fuchsia::net::Subnet ifaddr;
  auto svc = nl::Weave::DeviceLayer::PlatformMgrImpl().GetComponentContextForProcess()->svc();
  zx_status_t status = svc->Connect(stack_sync_ptr.NewRequest());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Connect to netstack failed: " << status;
    return kPlatformResultFailure;
  }
  status = GetInterface(stack_sync_ptr, &interface_id);
  if (status != ZX_OK) {
    return kPlatformResultFailure;
  }
  std::memcpy(v6.addr.data(), (uint8_t *)(in_address.Addr), v6.addr.size());
  addr.set_ipv6(v6);
  ifaddr.addr = std::move(addr);
  ifaddr.prefix_len = in_prefix_length;
  if (in_add) {
    fuchsia::net::stack::Stack_AddInterfaceAddress_Result result;
    status = stack_sync_ptr->AddInterfaceAddress(interface_id, std::move(ifaddr), &result);
    if (status != ZX_OK || result.is_err()) {
      FX_LOGS(ERROR) << "Failed to add interface address to id: " << interface_id
                     << " status: " << zx_status_get_string(status);
      return kPlatformResultFailure;
    }
  } else {
    fuchsia::net::stack::Stack_DelInterfaceAddress_Result result;
    status = stack_sync_ptr->DelInterfaceAddress(interface_id, std::move(ifaddr), &result);
    if (status != ZX_OK || result.is_err()) {
      FX_LOGS(ERROR) << "Failed to delete interface address for id: " << interface_id
                     << " status: " << zx_status_get_string(status);
      return kPlatformResultFailure;
    }
  }

  FX_LOGS(INFO) << "AddRemoveHostAddress successful";

  return kPlatformResultSuccess;
}

// Add or remove route to/from forwarding table.
PlatformResult AddRemoveHostRoute(InterfaceType in_interface_type, const Inet::IPPrefix &in_prefix,
                                  RoutePriority in_priority, bool in_add) {
  uint64_t interface_id = 0;
  fuchsia::net::stack::StackSyncPtr stack_sync_ptr;
  fuchsia::net::Ipv6Address v6;
  fuchsia::net::stack::ForwardingEntry entry;
  auto svc = nl::Weave::DeviceLayer::PlatformMgrImpl().GetComponentContextForProcess()->svc();
  zx_status_t status = svc->Connect(stack_sync_ptr.NewRequest());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Connect to netstack failed: " << status;
    return kPlatformResultFailure;
  }
  status = GetInterface(stack_sync_ptr, &interface_id);
  if (status != ZX_OK) {
    return kPlatformResultSuccess;
  }

  std::memcpy(v6.addr.data(), (uint8_t *)(in_prefix.IPAddr.Addr), v6.addr.size());
  if (in_add) {
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
    fuchsia::net::Subnet subnet;
    subnet.addr.set_ipv6(v6);
    subnet.prefix_len = in_prefix.Length;
    fuchsia::net::stack::Stack_DelForwardingEntry_Result result;
    status = stack_sync_ptr->DelForwardingEntry(std::move(subnet), &result);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "DelForwardingEntry failed: " << zx_status_get_string(status);
      return kPlatformResultFailure;
    }
    if (result.is_err()) {
      FX_LOGS(ERROR) << "DelForwardingEntry failed result";
      return kPlatformResultFailure;
    }
  }

  FX_LOGS(ERROR) << "AddRemoveHostRoute successful";
  return kPlatformResultSuccess;
}

#if WARM_CONFIG_SUPPORT_THREAD

PlatformResult AddRemoveThreadAddress(InterfaceType inInterfaceType,
                                      const Inet::IPAddress &inAddress, bool inAdd) {
  return kPlatformResultSuccess;
}

#endif  // WARM_CONFIG_SUPPORT_THREAD

#if WARM_CONFIG_SUPPORT_THREAD_ROUTING

#error "Weave Thread router support not implemented"

PlatformResult StartStopThreadAdvertisement(InterfaceType inInterfaceType,
                                            const Inet::IPPrefix &inPrefix, bool inStart) {
  // TODO: implement me
}

#endif  // WARM_CONFIG_SUPPORT_THREAD_ROUTING

#if WARM_CONFIG_SUPPORT_BORDER_ROUTING

#error "Weave border router support not implemented"

PlatformResult AddRemoveThreadRoute(InterfaceType inInterfaceType, const Inet::IPPrefix &inPrefix,
                                    RoutePriority inPriority, bool inAdd) {
  // TODO: implement me
}

PlatformResult SetThreadRoutePriority(InterfaceType inInterfaceType, const Inet::IPPrefix &inPrefix,
                                      RoutePriority inPriority) {
  // TODO: implement me
}

#endif  // WARM_CONFIG_SUPPORT_BORDER_ROUTING

}  // namespace Platform
}  // namespace Warm
}  // namespace Weave
}  // namespace nl
