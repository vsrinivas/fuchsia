// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_COMPONENTS_CPP_INCLUDE_WLAN_DRIVERS_COMPONENTS_NETWORK_PORT_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_COMPONENTS_CPP_INCLUDE_WLAN_DRIVERS_COMPONENTS_NETWORK_PORT_H_

#include <fidl/fuchsia.hardware.network/cpp/wire.h>
#include <fuchsia/hardware/network/device/cpp/banjo.h>
#include <fuchsia/hardware/network/mac/cpp/banjo.h>
#include <lib/stdcompat/span.h>
#include <lib/sync/cpp/completion.h>
#include <stdint.h>
#include <zircon/compiler.h>

#include <mutex>

namespace wlan::drivers::components {

// A network port corresponds to a network interface. A network device can provide multiple network
// ports. Such as a NIC with multiple RJ45 ports or in this case a WLAN device supporting multiple
// interfaces using a single device.
// The user should instantiate an object of this class and provide an implementation of
// NetworkPort::Callbacks to handle the various calls that are made to it. An implementation can
// choose to either inherit from NetworkPort (and optionally NetworkPort::Callbacks at the same
// time) or create a standalone object.
// A network port is created and registered with the network device when Init is called. The port
// is removed when the destructor of this class is called, i.e. when the object is destroyed.
class NetworkPort : public ::ddk::NetworkPortProtocol<NetworkPort>,
                    public ::ddk::MacAddrProtocol<NetworkPort> {
 public:
  class Callbacks {
   public:
    virtual ~Callbacks();

    // Called when the device needs to retrieve the port status. This mainly includes the port's
    // online status and MTU. This information is already filled out by NetworkPort so implementing
    // this method is optional but could be useful if the driver wants to provide additional
    // information. See port_status_t for more details about the information requested.
    virtual void PortGetStatus(port_status_t* out_status) {}

    // Called when a port is being removed from the network device. This is an optional method for
    // drivers that want to take extra action when this happens.
    virtual void PortRemoved() {}

    // Called when the network device needs to know the MTU of the port.
    virtual uint32_t PortGetMtu() = 0;

    // Called when the network device needs to know the MAC address of the port.
    virtual void MacGetAddress(uint8_t out_mac[6]) = 0;

    // Called when the network device needs to know about the features supported by the port. This
    // mostly relates to MAC filtering modes. See features_t for more details about possible
    // features.
    virtual void MacGetFeatures(features_t* out_features) = 0;

    // Called when the network device needs to set one of the supported MAC filtering modes from the
    // features call. See features_t for the different modes. When multicast filtering is enabled
    // then the driver should only accept unicast frames and multicast frames from the MAC addresses
    // specified in multicast_macs. Each 6-byte span in multicast_macs constitutes one MAC address
    // for this filter.
    virtual void MacSetMode(mode_t mode, cpp20::span<const uint8_t> multicast_macs) = 0;
  };

  enum class Role { Client, Ap };

  NetworkPort(network_device_ifc_protocol_t netdev_ifc, Callbacks& iface, uint8_t port_id);
  virtual ~NetworkPort();

  void Init(Role role);
  // Remove the port, this synchronously waits for the close to complete. After this no further
  // operations on the port are valid.
  void RemovePort();

  void SetPortOnline(bool online) __TA_EXCLUDES(online_mutex_);
  bool IsOnline() const __TA_EXCLUDES(online_mutex_);
  uint8_t PortId() const { return port_id_; }

  // NetworkPortProtocol implementation
  void NetworkPortGetInfo(port_info_t* out_info);
  void NetworkPortGetStatus(port_status_t* out_status) __TA_EXCLUDES(online_mutex_);
  void NetworkPortSetActive(bool active);
  void NetworkPortGetMac(mac_addr_protocol_t* out_mac_ifc);
  void NetworkPortRemoved() __TA_EXCLUDES(netdev_ifc_mutex_);

  // MacAddrProtocol implementation
  void MacAddrGetAddress(uint8_t out_mac[6]);
  void MacAddrGetFeatures(features_t* out_features);
  void MacAddrSetMode(mode_t mode, const uint8_t* multicast_macs_list, size_t multicast_macs_count);

 private:
  void GetPortStatusLocked(port_status_t* out_status) __TA_REQUIRES(online_mutex_);

  Role role_;
  Callbacks& iface_;
  mutable std::mutex netdev_ifc_mutex_;
  ::ddk::NetworkDeviceIfcProtocolClient netdev_ifc_ __TA_GUARDED(netdev_ifc_mutex_);
  uint8_t port_id_;
  mutable std::mutex online_mutex_;
  bool online_ __TA_GUARDED(online_mutex_) = false;
  libsync::Completion port_removed_;
};

}  // namespace wlan::drivers::components

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_COMPONENTS_CPP_INCLUDE_WLAN_DRIVERS_COMPONENTS_NETWORK_PORT_H_
