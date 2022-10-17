// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_ETHERNET_DRIVERS_ETHERNET_NETDEVICE_MIGRATION_NETDEVICE_MIGRATION_H_
#define SRC_CONNECTIVITY_ETHERNET_DRIVERS_ETHERNET_NETDEVICE_MIGRATION_NETDEVICE_MIGRATION_H_

#include <fidl/fuchsia.hardware.network/cpp/wire.h>
#include <fuchsia/hardware/ethernet/cpp/banjo.h>
#include <fuchsia/hardware/network/device/cpp/banjo.h>
#include <fuchsia/hardware/network/mac/cpp/banjo.h>
#include <zircon/system/public/zircon/compiler.h>

#include <queue>
#include <utility>

#include <ddktl/device.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>

#include "src/connectivity/network/drivers/network-device/device/public/locks.h"
#include "src/devices/lib/dev-operation/include/lib/operation/ethernet.h"
#include "src/lib/vmo_store/vmo_store.h"

namespace netdevice_migration {

using NetdeviceMigrationVmoStore = vmo_store::VmoStore<vmo_store::SlabStorage<uint32_t>>;
using Netbuf = eth::Operation<uint32_t>;
using NetbufPool = eth::OperationPool<uint32_t>;

class NetdeviceMigration;
using DeviceType = ddk::Device<NetdeviceMigration>;
class NetdeviceMigration
    : public DeviceType,
      public ddk::EthernetIfcProtocol<NetdeviceMigration>,
      public ddk::NetworkDeviceImplProtocol<NetdeviceMigration, ddk::base_protocol>,
      public ddk::NetworkPortProtocol<NetdeviceMigration>,
      public ddk::MacAddrProtocol<NetdeviceMigration> {
 public:
  static constexpr uint8_t kPortId = 13;
  // Equivalent to generic ethernet driver FIFO depth; see
  // eth::EthDev::kFifoDepth in //src/connectivity/ethernet/drivers/ethernet/ethernet.h.
  static constexpr uint32_t kFifoDepth = 256;
  static constexpr mode_t kSupportedMacFilteringModes =
      MODE_MULTICAST_FILTER | MODE_MULTICAST_PROMISCUOUS | MODE_PROMISCUOUS;
  static constexpr uint32_t kMulticastFilterMax = MAX_MAC_FILTER;
  static zx::result<std::unique_ptr<NetdeviceMigration>> Create(zx_device_t* dev);
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
  void EthernetIfcStatus(uint32_t status) __TA_EXCLUDES(status_lock_);
  void EthernetIfcRecv(const uint8_t* data_buffer, size_t data_size, uint32_t flags)
      __TA_EXCLUDES(rx_lock_) __TA_EXCLUDES(vmo_lock_);

  // For NetworkDeviceImplProtocol.
  zx_status_t NetworkDeviceImplInit(const network_device_ifc_protocol_t* iface);
  void NetworkDeviceImplStart(network_device_impl_start_callback callback, void* cookie)
      __TA_EXCLUDES(tx_lock_) __TA_EXCLUDES(rx_lock_);
  void NetworkDeviceImplStop(network_device_impl_stop_callback callback, void* cookie)
      __TA_EXCLUDES(tx_lock_) __TA_EXCLUDES(rx_lock_);
  void NetworkDeviceImplGetInfo(device_info_t* out_info);
  void NetworkDeviceImplQueueTx(const tx_buffer_t* buffers_list, size_t buffers_count);
  void NetworkDeviceImplQueueRxSpace(const rx_space_buffer_t* buffers_list, size_t buffers_count)
      __TA_EXCLUDES(rx_lock_);
  void NetworkDeviceImplPrepareVmo(uint8_t id, zx::vmo vmo,
                                   network_device_impl_prepare_vmo_callback callback, void* cookie)
      __TA_EXCLUDES(vmo_lock_);
  void NetworkDeviceImplReleaseVmo(uint8_t id) __TA_EXCLUDES(vmo_lock_);
  void NetworkDeviceImplSetSnoop(bool snoop);

  // For NetworkPortProtocol.
  void NetworkPortGetInfo(port_info_t* out_info);
  void NetworkPortGetStatus(port_status_t* out_status) __TA_EXCLUDES(status_lock_);
  void NetworkPortSetActive(bool active);
  void NetworkPortGetMac(mac_addr_protocol_t* out_mac_ifc);
  void NetworkPortRemoved();

  // For MacAddrProtocol.
  void MacAddrGetAddress(uint8_t out_mac[MAC_SIZE]);
  void MacAddrGetFeatures(features_t* out_features);
  void MacAddrSetMode(mode_t mode, const uint8_t* multicast_macs_list, size_t multicast_macs_count);

 private:
  NetdeviceMigration(zx_device_t* parent, ddk::EthernetImplProtocolClient ethernet, uint32_t mtu,
                     zx::bti eth_bti, vmo_store::Options opts, std::array<uint8_t, MAC_SIZE> mac,
                     size_t netbuf_size, NetbufPool netbuf_pool)
      : DeviceType(parent),
        ethernet_(ethernet),
        ethernet_ifc_proto_({&ethernet_ifc_protocol_ops_, this}),
        eth_bti_(std::move(eth_bti)),
        info_({
            .tx_depth = kFifoDepth,
            .rx_depth = kFifoDepth,
            .rx_threshold = kFifoDepth / 2,
            // Ensures clients do not use scatter-gather.
            .max_buffer_parts = 1,
            // Per fuchsia.hardware.network.device banjo API:
            // "Devices that do not support scatter-gather DMA may set this to a value smaller than
            // a page size to guarantee compatibility."
            .max_buffer_length = ZX_PAGE_SIZE / 2,
            // NetdeviceMigration has no alignment requirements.
            .buffer_alignment = 1,
            // Ensures that an rx buffer will always be large enough to the ethernet MTU.
            .min_rx_buffer_length = mtu,
        }),
        mtu_(mtu),
        mac_(mac),
        rx_types_{static_cast<uint8_t>(fuchsia_hardware_network::wire::FrameType::kEthernet)},
        tx_types_{tx_support_t{
            .type = static_cast<uint8_t>(fuchsia_hardware_network::wire::FrameType::kEthernet),
            .features = fuchsia_hardware_network::wire::kFrameFeaturesRaw}},
        port_info_(port_info_t{
            .port_class =
                static_cast<uint8_t>(fuchsia_hardware_network::wire::DeviceClass::kEthernet),
            .rx_types_list = rx_types_.data(),
            .rx_types_count = rx_types_.size(),
            .tx_types_list = tx_types_.data(),
            .tx_types_count = tx_types_.size(),
        }),
        netbuf_size_(netbuf_size),
        netbuf_pool_(std::move(netbuf_pool)),
        vmo_store_(std::move(opts)) {}
  void SetMacParam(uint32_t param, int32_t value, const uint8_t* data_buffer,
                   size_t data_size) const;

  std::atomic<size_t> no_rx_space_ = 0;

  ddk::NetworkDeviceIfcProtocolClient netdevice_;

  const ddk::EthernetImplProtocolClient ethernet_;
  const ethernet_ifc_protocol_t ethernet_ifc_proto_;
  const zx::bti eth_bti_;
  const device_info_t info_;
  const uint32_t mtu_;
  const std::array<uint8_t, MAC_SIZE> mac_;
  const std::array<uint8_t, 1> rx_types_;
  const std::array<tx_support_t, 1> tx_types_;
  const port_info_t port_info_;
  const size_t netbuf_size_;

  std::mutex status_lock_;
  fuchsia_hardware_network::wire::StatusFlags port_status_flags_ __TA_GUARDED(status_lock_);

  std::mutex tx_lock_ __TA_ACQUIRED_AFTER(rx_lock_, vmo_lock_);
  bool tx_started_ __TA_GUARDED(tx_lock_) = false;
  NetbufPool netbuf_pool_ __TA_GUARDED(tx_lock_);

  std::mutex rx_lock_ __TA_ACQUIRED_BEFORE(tx_lock_, vmo_lock_);
  bool rx_started_ __TA_GUARDED(rx_lock_) = false;
  // Use a queue to enforce FIFO ordering. With LIFO ordering, some buffers will sit unused unless
  // the driver hits buffer starvation, which could obscure bugs related to malformed buffers.
  std::queue<rx_space_buffer_t> rx_spaces_ __TA_GUARDED(rx_lock_);

  network::SharedLock vmo_lock_ __TA_ACQUIRED_BEFORE(tx_lock_) __TA_ACQUIRED_AFTER(rx_lock_);
  NetdeviceMigrationVmoStore vmo_store_ __TA_GUARDED(vmo_lock_);

  friend class NetdeviceMigrationTestHelper;
};

}  // namespace netdevice_migration

#endif  // SRC_CONNECTIVITY_ETHERNET_DRIVERS_ETHERNET_NETDEVICE_MIGRATION_NETDEVICE_MIGRATION_H_
