// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/network_watcher.h"

#include <lib/syslog/cpp/macros.h>

namespace forensics {
namespace crash_reports {

NetworkWatcher::NetworkWatcher(async_dispatcher_t* dispatcher,
                               std::shared_ptr<sys::ServiceDirectory> services)
    : dispatcher_(dispatcher),
      services_(std::move(services)),
      netstack_(),
      backoff_(/*initial_delay=*/zx::min(1), /*retry_factor=*/2u,
               /*max_delay=*/zx::hour(1)),
      callbacks_() {
  netstack_.set_error_handler([this](const zx_status_t status) {
    FX_PLOGS(WARNING, status) << "Lost connection to " << fuchsia::netstack::Netstack::Name_;
    watch_task_.PostDelayed(dispatcher_, backoff_.GetNext());
  });

  Watch();
}

void NetworkWatcher::Register(fit::function<void(bool)> on_reachable) {
  callbacks_.push_back(std::move(on_reachable));
}

void NetworkWatcher::Watch() {
  if (!netstack_.is_bound()) {
    services_->Connect(netstack_.NewRequest(dispatcher_));
  }

  auto IsReachable = [](const fuchsia::netstack::NetInterface& interface) {
    if ((interface.flags & fuchsia::netstack::Flags::UP) != fuchsia::netstack::Flags::UP) {
      return false;
    }
    if ((interface.flags & fuchsia::netstack::Flags::DHCP) != fuchsia::netstack::Flags::DHCP) {
      return false;
    }
    auto isZero = [](const uint8_t octet) { return octet == 0; };
    switch (interface.addr.Which()) {
      case fuchsia::net::IpAddress::Tag::kIpv4: {
        const auto& octets = interface.addr.ipv4().addr;
        return !std::all_of(octets.cbegin(), octets.cend(), isZero);
      }
      case fuchsia::net::IpAddress::Tag::kIpv6: {
        const auto& octets = interface.addr.ipv6().addr;
        return !std::all_of(octets.cbegin(), octets.cend(), isZero);
      }
      case fuchsia::net::IpAddress::Tag::Invalid: {
        FX_LOGS(ERROR) << "Network interface " << interface.name << " has malformed IP address";
        return false;
      }
    }
  };

  netstack_.events().OnInterfacesChanged =
      [this, IsReachable](std::vector<fuchsia::netstack::NetInterface> interfaces) {
        backoff_.Reset();
        const bool reachable = std::any_of(interfaces.cbegin(), interfaces.cend(), IsReachable);
        for (const auto& on_reachable : callbacks_) {
          on_reachable(reachable);
        }
      };
}

}  // namespace crash_reports
}  // namespace forensics
