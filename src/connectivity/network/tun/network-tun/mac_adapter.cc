// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mac_adapter.h"

#include <lib/sync/completion.h>

#include <fbl/auto_lock.h>

namespace network {
namespace tun {

zx_status_t MacAdapter::Create(MacAdapterParent* parent, fuchsia::net::MacAddress mac,
                               bool promisc_only, std::unique_ptr<MacAdapter>* out) {
  std::unique_ptr<MacAdapter> adapter(new MacAdapter(parent, mac, promisc_only));
  mac_addr_impl_protocol_t proto = {&adapter->mac_addr_impl_protocol_ops_, adapter.get()};
  zx_status_t status =
      MacAddrDeviceInterface::Create(ddk::MacAddrImplProtocolClient(&proto), &adapter->device_);
  if (status == ZX_OK) {
    *out = std::move(adapter);
  }
  return status;
}

zx_status_t MacAdapter::Bind(async_dispatcher_t* dispatcher, zx::channel req) {
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

void MacAdapter::MacAddrImplGetAddress(uint8_t* out_mac) {
  std::copy(mac_.octets.begin(), mac_.octets.end(), out_mac);
}

void MacAdapter::MacAddrImplGetFeatures(features_t* out_features) {
  if (promisc_only_) {
    out_features->multicast_filter_count = 0;
    out_features->supported_modes = MODE_MULTICAST_PROMISCUOUS;
  } else {
    out_features->multicast_filter_count = fuchsia::net::tun::MAX_MULTICAST_FILTERS;
    out_features->supported_modes =
        MODE_PROMISCUOUS | MODE_MULTICAST_FILTER | MODE_MULTICAST_PROMISCUOUS;
  }
}

void MacAdapter::MacAddrImplSetMode(mode_t mode, const uint8_t* multicast_macs_list,
                                    size_t multicast_macs_count) {
  fbl::AutoLock lock(&state_lock_);
  fuchsia::hardware::network::MacFilterMode filter_mode{};
  switch (mode) {
    case MODE_PROMISCUOUS:
      filter_mode = fuchsia::hardware::network::MacFilterMode::PROMISCUOUS;
      break;
    case MODE_MULTICAST_PROMISCUOUS:
      filter_mode = fuchsia::hardware::network::MacFilterMode::MULTICAST_PROMISCUOUS;
      break;
    case MODE_MULTICAST_FILTER:
      filter_mode = fuchsia::hardware::network::MacFilterMode::MULTICAST_FILTER;
      break;
    default:
      ZX_ASSERT_MSG(false, "Unexpected filter mode %d", mode);
      break;
  }
  mac_state_.set_mode(filter_mode);
  std::vector<fuchsia::net::MacAddress> filters;
  filters.reserve(multicast_macs_count);
  while (multicast_macs_count--) {
    auto& n = filters.emplace_back();
    std::copy_n(multicast_macs_list, n.octets.size(), n.octets.begin());
    multicast_macs_list += n.octets.size();
  }
  mac_state_.set_multicast_filters(std::move(filters));
  parent_->OnMacStateChanged(this);
}

void MacAdapter::CloneMacState(fuchsia::net::tun::MacState* out) {
  fbl::AutoLock lock(&state_lock_);
  mac_state_.Clone(out);
}

}  // namespace tun
}  // namespace network
