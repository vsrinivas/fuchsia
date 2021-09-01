// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_ETHERNET_DRIVERS_ETHERNET_NETDEVICE_MIGRATION_NETDEVICE_MIGRATION_H_
#define SRC_CONNECTIVITY_ETHERNET_DRIVERS_ETHERNET_NETDEVICE_MIGRATION_NETDEVICE_MIGRATION_H_

#include <fidl/fuchsia.hardware.network/cpp/wire.h>
#include <fuchsia/hardware/ethernet/cpp/banjo.h>
#include <fuchsia/hardware/network/device/cpp/banjo.h>
#include <zircon/system/public/zircon/compiler.h>

#include <ddktl/device.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>

#include "src/connectivity/network/drivers/network-device/device/public/locks.h"
#include "src/lib/vmo_store/vmo_store.h"

namespace netdevice_migration {

using NetdeviceMigrationVmoStore = vmo_store::VmoStore<vmo_store::SlabStorage<uint8_t>>;

class NetdeviceMigration;
using DeviceType = ddk::Device<NetdeviceMigration>;
class NetdeviceMigration : public DeviceType,
                           public ddk::EthernetIfcProtocol<NetdeviceMigration>,
                           public ddk::NetworkDeviceImplProtocol<NetdeviceMigration>,
                           public ddk::NetworkPortProtocol<NetdeviceMigration> {
 public:
  // TODO(https://fxbug.dev/64310): Change value once Netstack no longer assumes all devices have a
  // single port with id 0.
  static constexpr uint8_t kPortId = 0;
  static zx::status<std::unique_ptr<NetdeviceMigration>> Create(zx_device_t* dev);
  virtual ~NetdeviceMigration() = default;

  // Initializes the driver and binds it to the parent device `dev`. The DDK calls Bind through
  // the zx_driver_ops_t published for this driver; consequently, a client of this driver will not
  // need to directly call this function.
  static zx_status_t Bind(void* ctx, zx_device_t* dev);
  // Adds the driver to device manager.
  zx_status_t DeviceAdd();

  // For DeviceType.
  void DdkRelease();

  // For EthernetIfcProtocol.
  void EthernetIfcStatus(uint32_t status) __TA_EXCLUDES(lock_);
  void EthernetIfcRecv(const uint8_t* data_buffer, size_t data_size, uint32_t flags);

  // For NetworkDeviceImplProtocol.
  zx_status_t NetworkDeviceImplInit(const network_device_ifc_protocol_t* iface);
  void NetworkDeviceImplStart(network_device_impl_start_callback callback, void* cookie)
      __TA_EXCLUDES(lock_);
  void NetworkDeviceImplStop(network_device_impl_stop_callback callback, void* cookie)
      __TA_EXCLUDES(lock_);
  void NetworkDeviceImplGetInfo(device_info_t* out_info);
  void NetworkDeviceImplQueueTx(const tx_buffer_t* buffers_list, size_t buffers_count);
  void NetworkDeviceImplQueueRxSpace(const rx_space_buffer_t* buffers_list, size_t buffers_count);
  void NetworkDeviceImplPrepareVmo(uint8_t id, zx::vmo vmo) __TA_EXCLUDES(vmo_lock_);
  void NetworkDeviceImplReleaseVmo(uint8_t id) __TA_EXCLUDES(vmo_lock_);
  void NetworkDeviceImplSetSnoop(bool snoop);

  // For NetworkPortProtocol.
  void NetworkPortGetInfo(port_info_t* out_info);
  void NetworkPortGetStatus(port_status_t* out_status) __TA_EXCLUDES(lock_);
  void NetworkPortSetActive(bool active);
  void NetworkPortGetMac(mac_addr_protocol_t* out_mac_ifc);
  void NetworkPortRemoved();

  // Returns true iff the driver is ready to send and receive frames.
  bool IsStarted() __TA_EXCLUDES(lock_);
  const ethernet_ifc_protocol_t& EthernetIfcProto() const { return ethernet_ifc_proto_; }
  const zx::bti& Bti() const { return eth_bti_; }
  template <typename T, typename F>
  T WithVmoStore(F fn) __TA_EXCLUDES(vmo_lock_) {
    fbl::AutoLock lock(&vmo_lock_);
    NetdeviceMigrationVmoStore& vmo_store = vmo_store_;
    return fn(vmo_store);
  }

 private:
  NetdeviceMigration(zx_device_t* parent, ddk::EthernetImplProtocolClient ethernet, zx::bti eth_bti,
                     vmo_store::Options opts)
      : DeviceType(parent),
        ethernet_(ethernet),
        ethernet_ifc_proto_({&ethernet_ifc_protocol_ops_, this}),
        eth_bti_(std::move(eth_bti)),
        vmo_store_(opts) {}

  ddk::NetworkDeviceIfcProtocolClient netdevice_;

  const ddk::EthernetImplProtocolClient ethernet_;
  const ethernet_ifc_protocol_t ethernet_ifc_proto_;
  const zx::bti eth_bti_;

  fbl::Mutex lock_;
  bool started_ __TA_GUARDED(lock_) = false;
  fuchsia_hardware_network::wire::StatusFlags port_status_flags_ __TA_GUARDED(lock_);

  network::SharedLock vmo_lock_;
  NetdeviceMigrationVmoStore vmo_store_ __TA_GUARDED(vmo_lock_);
};

}  // namespace netdevice_migration

#endif  // SRC_CONNECTIVITY_ETHERNET_DRIVERS_ETHERNET_NETDEVICE_MIGRATION_NETDEVICE_MIGRATION_H_
