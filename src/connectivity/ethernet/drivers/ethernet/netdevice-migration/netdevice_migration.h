// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_ETHERNET_DRIVERS_ETHERNET_NETDEVICE_MIGRATION_NETDEVICE_MIGRATION_H_
#define SRC_CONNECTIVITY_ETHERNET_DRIVERS_ETHERNET_NETDEVICE_MIGRATION_NETDEVICE_MIGRATION_H_

#include <fuchsia/hardware/ethernet/cpp/banjo.h>
#include <fuchsia/hardware/network/device/cpp/banjo.h>

#include <ddktl/device.h>

namespace netdevice_migration {

class NetdeviceMigration;
using DeviceType = ddk::Device<NetdeviceMigration>;
class NetdeviceMigration : public DeviceType,
                           public ddk::EthernetIfcProtocol<NetdeviceMigration>,
                           public ddk::NetworkDeviceImplProtocol<NetdeviceMigration> {
 public:
  explicit NetdeviceMigration(zx_device_t* parent) : DeviceType(parent), ethernet_(parent) {}
  virtual ~NetdeviceMigration() = default;

  // Initializes the driver and binds it to the parent device `dev`. The DDK calls Bind through
  // the zx_driver_ops_t published for this driver; consequently, a client of this driver will not
  // need to directly call this function.
  static zx_status_t Bind(void* ctx, zx_device_t* dev);
  // Initializes the driver's client to the parent EthernetImplProtocol and adds the driver to
  // devmgr.
  zx_status_t Init();

  // For DeviceType.
  void DdkRelease();

  // For EthernetIfcProtocol.
  void EthernetIfcStatus(uint32_t status);
  void EthernetIfcRecv(const uint8_t* data_buffer, size_t data_size, uint32_t flags);

  // For NetworkDeviceImplProtocol.
  zx_status_t NetworkDeviceImplInit(const network_device_ifc_protocol_t* iface);
  void NetworkDeviceImplStart(network_device_impl_start_callback callback, void* cookie);
  void NetworkDeviceImplStop(network_device_impl_stop_callback callback, void* cookie);
  void NetworkDeviceImplGetInfo(device_info_t* out_info);
  void NetworkDeviceImplQueueTx(const tx_buffer_t* buffers_list, size_t buffers_count);
  void NetworkDeviceImplQueueRxSpace(const rx_space_buffer_t* buffers_list, size_t buffers_count);
  void NetworkDeviceImplPrepareVmo(uint8_t id, zx::vmo vmo);
  void NetworkDeviceImplReleaseVmo(uint8_t id);
  void NetworkDeviceImplSetSnoop(bool snoop);

 private:
  ddk::EthernetImplProtocolClient ethernet_;
  ddk::NetworkDeviceIfcProtocolClient netdevice_;
  ethernet_info_t eth_info_;
  zx::bti eth_bti_;
};

}  // namespace netdevice_migration

#endif  // SRC_CONNECTIVITY_ETHERNET_DRIVERS_ETHERNET_NETDEVICE_MIGRATION_NETDEVICE_MIGRATION_H_
