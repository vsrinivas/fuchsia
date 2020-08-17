// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/testing/stubs/network_reachability_provider.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>

namespace forensics {
namespace stubs {

void NetworkReachabilityProvider::TriggerOnNetworkReachable(const bool reachable) {
  FX_CHECK(binding()) << "No client is connected to the stub server yet";
  std::vector<fuchsia::netstack::NetInterface> interfaces;
  if (reachable) {
    interfaces.emplace_back(fuchsia::netstack::NetInterface{
        .flags = fuchsia::netstack::Flags::UP | fuchsia::netstack::Flags::DHCP,
        .addr = fuchsia::net::IpAddress::WithIpv4(fuchsia::net::Ipv4Address{
            .addr = {1},
        }),
        .netmask = fuchsia::net::IpAddress::WithIpv4(fuchsia::net::Ipv4Address{}),
        .broadaddr = fuchsia::net::IpAddress::WithIpv4(fuchsia::net::Ipv4Address{}),
    });
  }
  binding()->events().OnInterfacesChanged(std::move(interfaces));
}

}  // namespace stubs
}  // namespace forensics
