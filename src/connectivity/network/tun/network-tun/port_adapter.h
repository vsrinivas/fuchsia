// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_CONNECTIVITY_NETWORK_TUN_NETWORK_TUN_PORT_ADAPTER_H_
#define SRC_CONNECTIVITY_NETWORK_TUN_NETWORK_TUN_PORT_ADAPTER_H_

#include <fuchsia/hardware/network/device/cpp/banjo.h>

#include <fbl/mutex.h>

#include "config.h"
#include "mac_adapter.h"

namespace network {
namespace tun {

class PortAdapter;

// An abstract PortAdapter parent.
//
// This abstract class allows the owner of a `PortAdapter` to change its behavior and be notified
// of important events.
class PortAdapterParent : public MacAdapterParent {
 public:
  ~PortAdapterParent() override = default;

  // Called when the device's `has_session` state changes.
  virtual void OnHasSessionsChanged(PortAdapter& port) = 0;
  // Called when the port's status changes.
  //
  // `new_status` must be reported to the device containing the port.
  virtual void OnPortStatusChanged(PortAdapter& port, const port_status_t& new_status) = 0;
  // Called when the port is destroyed and completely removed from the device.
  virtual void OnPortDestroyed(PortAdapter& port) = 0;
};

// An adapter for `NetworkPort`.
//
// `PortAdapter` is used to provide the business logic of virtual `NetworkPort` implementations
// both for `tun.Device` and `tun.DevicePair` device classes.
class PortAdapter : public ddk::NetworkPortProtocol<PortAdapter> {
 public:
  PortAdapter(PortAdapterParent* parent, const BasePortConfig& config,
              std::unique_ptr<MacAdapter> mac);
  PortAdapter(PortAdapter&&) = delete;

  // NetworkPort protocol:
  void NetworkPortGetInfo(port_info_t* out_info);
  void NetworkPortGetStatus(port_status_t* out_status);
  void NetworkPortSetActive(bool active);
  void NetworkPortGetMac(mac_addr_protocol_t* out_mac_ifc);
  void NetworkPortRemoved();

  // Sets this port's emulated `online` status.
  //
  // Returns true if the online status changed.
  bool SetOnline(bool online);
  bool online();
  bool has_sessions();
  uint32_t mtu() const { return mtu_; }
  const std::unique_ptr<MacAdapter>& mac() const { return mac_; }
  uint8_t id() const { return port_id_; }
  network_port_protocol_t proto() { return {.ops = &network_port_protocol_ops_, .ctx = this}; }

 private:
  std::array<uint8_t, fuchsia_hardware_network::wire::kMaxFrameTypes> rx_types_;
  std::array<tx_support_t, fuchsia_hardware_network::wire::kMaxFrameTypes> tx_types_;
  // Pointer to parent, not owned.
  PortAdapterParent* const parent_;
  const uint8_t port_id_;
  const uint32_t mtu_;
  const std::unique_ptr<MacAdapter> mac_;
  const port_info_t port_info_;

  fbl::Mutex state_lock_;
  bool has_sessions_ __TA_GUARDED(state_lock_) = false;
  bool online_ __TA_GUARDED(state_lock_) = false;
};

}  // namespace tun
}  // namespace network

#endif  // SRC_CONNECTIVITY_NETWORK_TUN_NETWORK_TUN_PORT_ADAPTER_H_
