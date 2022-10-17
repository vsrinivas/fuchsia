// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mac_adapter.h"

#include <lib/sync/completion.h>

#include <fbl/auto_lock.h>

namespace network {
namespace tun {

zx::result<std::unique_ptr<MacAdapter>> MacAdapter::Create(MacAdapterParent* parent,
                                                           fuchsia_net::wire::MacAddress mac,
                                                           bool promisc_only) {
  fbl::AllocChecker ac;
  std::unique_ptr<MacAdapter> adapter(new (&ac) MacAdapter(parent, mac, promisc_only));
  if (!ac.check()) {
    return zx::error(ZX_ERR_NO_MEMORY);
  }

  mac_addr_protocol_t proto = adapter->proto();
  zx::result device = MacAddrDeviceInterface::Create(ddk::MacAddrProtocolClient(&proto));
  if (device.is_error()) {
    return device.take_error();
  }
  adapter->device_ = std::move(device.value());
  return zx::ok(std::move(adapter));
}

zx_status_t MacAdapter::Bind(async_dispatcher_t* dispatcher,
                             fidl::ServerEnd<netdev::MacAddressing> req) {
  return device_->Bind(dispatcher, std::move(req));
}

void MacAdapter::Teardown(fit::callback<void()> callback) {
  device_->Teardown(std::move(callback));
}

void MacAdapter::TeardownSync() {
  sync_completion_t completion;
  Teardown([&completion]() { sync_completion_signal(&completion); });
  sync_completion_wait_deadline(&completion, ZX_TIME_INFINITE);
}

void MacAdapter::MacAddrGetAddress(uint8_t* out_mac) {
  std::copy(mac_.octets.begin(), mac_.octets.end(), out_mac);
}

void MacAdapter::MacAddrGetFeatures(features_t* out_features) {
  if (promisc_only_) {
    out_features->multicast_filter_count = 0;
    out_features->supported_modes = MODE_MULTICAST_PROMISCUOUS;
  } else {
    out_features->multicast_filter_count = fuchsia_net_tun::wire::kMaxMulticastFilters;
    out_features->supported_modes =
        MODE_PROMISCUOUS | MODE_MULTICAST_FILTER | MODE_MULTICAST_PROMISCUOUS;
  }
}

void MacAdapter::MacAddrSetMode(mode_t mode, const uint8_t* multicast_macs_list,
                                size_t multicast_macs_count) {
  fbl::AutoLock lock(&state_lock_);
  fuchsia_hardware_network::wire::MacFilterMode filter_mode;
  switch (mode) {
    case MODE_PROMISCUOUS:
      filter_mode = fuchsia_hardware_network::wire::MacFilterMode::kPromiscuous;
      break;
    case MODE_MULTICAST_PROMISCUOUS:
      filter_mode = fuchsia_hardware_network::wire::MacFilterMode::kMulticastPromiscuous;
      break;
    case MODE_MULTICAST_FILTER:
      filter_mode = fuchsia_hardware_network::wire::MacFilterMode::kMulticastFilter;
      break;
    default:
      ZX_ASSERT_MSG(false, "Unexpected filter mode %d", mode);
  }
  mac_state_.mode = filter_mode;
  mac_state_.multicast_filters.clear();
  mac_state_.multicast_filters.reserve(multicast_macs_count);
  while (multicast_macs_count--) {
    auto& n = mac_state_.multicast_filters.emplace_back();
    std::copy_n(multicast_macs_list, n.octets.size(), n.octets.begin());
    multicast_macs_list += n.octets.size();
  }
  parent_->OnMacStateChanged(this);
}

MacState MacAdapter::GetMacState() {
  fbl::AutoLock lock(&state_lock_);
  return mac_state_;
}

}  // namespace tun
}  // namespace network
