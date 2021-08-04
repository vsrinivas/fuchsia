// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "port_adapter.h"

#include <lib/syslog/global.h>

#include <fbl/auto_lock.h>

namespace network {
namespace tun {

void PortAdapter::NetworkPortGetInfo(port_info_t* out_info) { *out_info = port_info_; }
void PortAdapter::NetworkPortGetStatus(port_status_t* out_status) {
  fbl::AutoLock lock(&state_lock_);
  *out_status = {
      .mtu = mtu_,
      .flags =
          online_ ? static_cast<uint32_t>(fuchsia_hardware_network::wire::StatusFlags::kOnline) : 0,
  };
}
void PortAdapter::NetworkPortSetActive(bool active) {
  fbl::AutoLock lock(&state_lock_);
  if (active != has_sessions_) {
    has_sessions_ = active;
    lock.release();
    parent_->OnHasSessionsChanged(*this);
  }
}
void PortAdapter::NetworkPortGetMac(mac_addr_protocol_t* out_mac_ifc) {
  if (mac_) {
    *out_mac_ifc = mac_->proto();
  } else {
    *out_mac_ifc = {};
  }
}
void PortAdapter::NetworkPortRemoved() { parent_->OnPortDestroyed(*this); }

PortAdapter::PortAdapter(PortAdapterParent* parent, const BasePortConfig& config,
                         std::unique_ptr<MacAdapter> mac)
    : parent_(parent),
      port_id_(config.port_id),
      mtu_(config.mtu),
      mac_(std::move(mac)),
      port_info_(port_info_t{
          .port_class = static_cast<uint8_t>(fuchsia_hardware_network::wire::DeviceClass::kVirtual),
          .rx_types_list = rx_types_.data(),
          .rx_types_count = config.rx_types.size(),
          .tx_types_list = tx_types_.data(),
          .tx_types_count = config.tx_types.size(),
      }) {
  // Initialize rx_types_ and tx_types_ lists from config.
  for (size_t i = 0; i < config.rx_types.size(); i++) {
    rx_types_[i] = static_cast<uint8_t>(config.rx_types[i]);
  }
  for (size_t i = 0; i < config.tx_types.size(); i++) {
    tx_types_[i].features = config.tx_types[i].features;
    tx_types_[i].type = static_cast<uint8_t>(config.tx_types[i].type);
    tx_types_[i].supported_flags = static_cast<uint32_t>(config.tx_types[i].supported_flags);
  }
}

bool PortAdapter::SetOnline(bool online) {
  fbl::AutoLock lock(&state_lock_);
  if (online == online_) {
    return false;
  }
  online_ = online;
  port_status_t new_status = {
      .mtu = mtu_,
      .flags =
          online_ ? static_cast<uint32_t>(fuchsia_hardware_network::wire::StatusFlags::kOnline) : 0,
  };
  parent_->OnPortStatusChanged(*this, new_status);
  return true;
}

bool PortAdapter::online() {
  fbl::AutoLock lock(&state_lock_);
  return online_;
}

bool PortAdapter::has_sessions() {
  fbl::AutoLock lock(&state_lock_);
  return has_sessions_;
}

}  // namespace tun
}  // namespace network
