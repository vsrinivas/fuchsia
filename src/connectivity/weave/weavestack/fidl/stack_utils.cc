// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
#include <arpa/inet.h>
#pragma GCC diagnostic push
#include <Weave/DeviceLayer/PlatformManager.h>
#include <Weave/DeviceLayer/ConfigurationManager.h>
#include <Weave/DeviceLayer/ConnectivityManager.h>
#pragma GCC diagnostic pop
// clang-format on

namespace weavestack {
namespace {
using fuchsia::net::IpAddress;
using fuchsia::net::Ipv4Address;
using fuchsia::net::Ipv6Address;

using fuchsia::weave::Host;
using fuchsia::weave::PairingState;

using nl::Weave::DeviceLayer::ConfigurationMgr;
using nl::Weave::DeviceLayer::ConnectivityMgr;
}  // namespace

PairingState CurrentPairingState() {
  fuchsia::weave::PairingState pairing_state;

  // Set individual profile/component provisioning state
  pairing_state.set_is_wlan_provisioned(ConnectivityMgr().IsWiFiStationProvisioned());
  pairing_state.set_is_thread_provisioned(ConnectivityMgr().IsThreadProvisioned());
  pairing_state.set_is_fabric_provisioned(ConfigurationMgr().IsMemberOfFabric());
  pairing_state.set_is_service_provisioned(ConfigurationMgr().IsPairedToAccount());

  // Set overall provisioning state
  pairing_state.set_is_weave_fully_provisioned(ConfigurationMgr().IsFullyProvisioned());

  return pairing_state;
}

Host HostFromHostname(std::string hostname) {
  constexpr char kIpV4Chars[] = "0123456789.";
  constexpr char kIpV6Chars[] = "0123456789.abcdefABCDEF:";

  if (hostname.find_first_not_of(kIpV4Chars) == std::string::npos) {
    // Only consists of ipv4 characters, attempt to parse as ipv4
    Ipv4Address address;

    static_assert(sizeof(struct in_addr) == sizeof(address.addr),
                  "Cannot write results of inet_pton directly to IpAddress");
    if (inet_pton(AF_INET, hostname.c_str(), &address.addr) == 1) {
      return Host::WithIpAddress(IpAddress::WithIpv4(std::move(address)));
    }
    // If reached, parse failed, assume hostname
  } else if (hostname.find_first_not_of(kIpV6Chars) == std::string::npos) {
    // Only consists of ipv6 characters, attempt to parse as ipv6
    Ipv6Address address;

    static_assert(sizeof(struct in6_addr) == sizeof(address.addr),
                  "Cannot write results of inet_pton directly to IpAddress");
    if (inet_pton(AF_INET6, hostname.c_str(), &address.addr) == 1) {
      return Host::WithIpAddress(IpAddress::WithIpv6(std::move(address)));
    }
    // If reached, parse failed, assume hostname
  }

  // Non-IP-address characters or failed parse, assume hostname
  return Host::WithHostname(std::move(hostname));
}

}  // namespace weavestack
