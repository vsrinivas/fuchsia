// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/connectivity/wlan/drivers/lib/components/cpp/include/wlan/drivers/components/network_port.h"

#include <lib/ddk/debug.h>

namespace wlan::drivers::components {

NetworkPort::Callbacks::~Callbacks() = default;

NetworkPort::NetworkPort(network_device_ifc_protocol_t netdev_ifc, Callbacks& iface,
                         uint8_t port_id)
    : iface_(iface), netdev_ifc_(&netdev_ifc), port_id_(port_id) {}

NetworkPort::~NetworkPort() { RemovePort(); }

void NetworkPort::Init(Role role) {
  std::lock_guard lock(netdev_ifc_mutex_);
  role_ = role;
  if (!netdev_ifc_.is_valid()) {
    zxlogf(WARNING, "netdev_ifc_ invalid, port likely removed.");
    return;
  }
  netdev_ifc_.AddPort(port_id_, this, &network_port_protocol_ops_);
}

void NetworkPort::RemovePort() {
  std::lock_guard lock(netdev_ifc_mutex_);
  if (!netdev_ifc_.is_valid()) {
    zxlogf(WARNING, "netdev_ifc_ invalid, port likely removed.");
    return;
  }
  netdev_ifc_.RemovePort(port_id_);
  zx_status_t status = port_removed_.Wait();
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to wait for port removed completion: %s", zx_status_get_string(status));
  }
  netdev_ifc_.clear();
}

void NetworkPort::SetPortOnline(bool online) {
  std::lock_guard online_lock(online_mutex_);
  if (online_ == online) {
    return;
  }
  online_ = online;
  port_status_t status;
  GetPortStatusLocked(&status);
  std::lock_guard netdev_ifc_lock(netdev_ifc_mutex_);
  if (netdev_ifc_.is_valid()) {
    netdev_ifc_.PortStatusChanged(port_id_, &status);
  } else {
    zxlogf(WARNING, "netdev_ifc_ invalid, port likely removed.");
  }
}

bool NetworkPort::IsOnline() const {
  std::lock_guard lock(online_mutex_);
  return online_;
}

void NetworkPort::NetworkPortGetInfo(port_info_t* out_info) {
  static constexpr uint8_t kSupportedRxTypes[] = {
      static_cast<uint8_t>(fuchsia_hardware_network::wire::FrameType::kEthernet)};

  static constexpr tx_support_t kSupportedTxTypes[]{{
      .type = static_cast<uint8_t>(fuchsia_hardware_network::wire::FrameType::kEthernet),
      .features = fuchsia_hardware_network::wire::kFrameFeaturesRaw,
      .supported_flags = 0,
  }};

  switch (role_) {
    case Role::Client:
      out_info->port_class =
          static_cast<uint8_t>(fuchsia_hardware_network::wire::DeviceClass::kWlan);
      break;
    case Role::Ap:
      out_info->port_class =
          static_cast<uint8_t>(fuchsia_hardware_network::wire::DeviceClass::kWlanAp);
      break;
  }
  out_info->rx_types_list = kSupportedRxTypes;
  out_info->rx_types_count = std::size(kSupportedRxTypes);
  out_info->tx_types_list = kSupportedTxTypes;
  out_info->tx_types_count = std::size(kSupportedTxTypes);
}

void NetworkPort::NetworkPortGetStatus(port_status_t* out_status) {
  std::lock_guard lock(online_mutex_);
  GetPortStatusLocked(out_status);
}

void NetworkPort::NetworkPortSetActive(bool active) {}

void NetworkPort::NetworkPortGetMac(mac_addr_protocol_t* out_mac_ifc) {
  *out_mac_ifc = {
      .ops = &mac_addr_protocol_ops_,
      .ctx = this,
  };
}

void NetworkPort::NetworkPortRemoved() {
  iface_.PortRemoved();
  port_removed_.Signal();
}

void NetworkPort::MacAddrGetAddress(uint8_t out_mac[6]) { iface_.MacGetAddress(out_mac); }

void NetworkPort::MacAddrGetFeatures(features_t* out_features) {
  iface_.MacGetFeatures(out_features);
}

void NetworkPort::MacAddrSetMode(mode_t mode, const uint8_t* multicast_macs_list,
                                 size_t multicast_macs_count) {
  iface_.MacSetMode(mode, cpp20::span<const uint8_t>(multicast_macs_list, multicast_macs_count));
}

void NetworkPort::GetPortStatusLocked(port_status_t* out_status) {
  // Provide a reasonable default status
  using fuchsia_hardware_network::wire::StatusFlags;
  *out_status = {
      .mtu = iface_.PortGetMtu(),
      .flags = online_ ? static_cast<uint32_t>(StatusFlags::kOnline) : 0u,
  };

  // Allow the interface implementation to modify the status if it wants to
  iface_.PortGetStatus(out_status);
}

}  // namespace wlan::drivers::components
