// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_ETHERNET_DRIVERS_VIRTIO_NETDEVICE_H_
#define SRC_CONNECTIVITY_ETHERNET_DRIVERS_VIRTIO_NETDEVICE_H_

#include <fidl/fuchsia.hardware.network/cpp/wire.h>
#include <fuchsia/hardware/network/device/cpp/banjo.h>
#include <fuchsia/hardware/network/mac/cpp/banjo.h>
#include <lib/ddk/io-buffer.h>
#include <lib/virtio/device.h>
#include <lib/virtio/ring.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <memory>
#include <mutex>
#include <shared_mutex>

#include <ddktl/device.h>
#include <fbl/macros.h>
#include <virtio/net.h>

#include "src/connectivity/network/drivers/network-device/device/public/locks.h"
#include "src/lib/vmo_store/vmo_store.h"

namespace virtio {

using VmoStore = vmo_store::VmoStore<vmo_store::SlabStorage<uint32_t>>;

class NetworkDevice;

using DeviceType = ddk::Device<NetworkDevice, ddk::Unbindable>;
class NetworkDevice : public Device,
                      // Mixins for protocol device:
                      public DeviceType,
                      // Mixin for Network device banjo protocol:
                      public ddk::NetworkDeviceImplProtocol<NetworkDevice, ddk::base_protocol>,
                      public ddk::NetworkPortProtocol<NetworkDevice>,
                      public ddk::MacAddrProtocol<NetworkDevice> {
 public:
  // Specifies how many packets can fit in each of the receive and transmit
  // backlogs.
  // Chosen arbitrarily. Larger values will cause increased memory consumption,
  // lower values may cause ring underruns.
  static constexpr size_t kBacklog = 256;
  // The single port ID created by this device.
  static constexpr uint8_t kPortId = 1;
  // Specifies the maximum transfer unit we support.
  // Picked to mimic common default ethernet frame size.
  static constexpr size_t kMtu = 1514;
  static constexpr size_t kFrameSize = sizeof(virtio_net_hdr_t) + kMtu;

  // Queue identifiers.
  static constexpr uint16_t kRxId = 0u;
  static constexpr uint16_t kTxId = 1u;

  NetworkDevice(zx_device_t* device, zx::bti, std::unique_ptr<Backend> backend);
  virtual ~NetworkDevice();

  zx_status_t Init() override __TA_EXCLUDES(state_lock_);
  void DdkRelease() __TA_EXCLUDES(state_lock_);
  void DdkUnbind(ddk::UnbindTxn txn) { virtio::Device::Unbind(std::move(txn)); }

  // VirtIO callbacks
  void IrqRingUpdate() override __TA_EXCLUDES(state_lock_);
  void IrqConfigChange() override __TA_EXCLUDES(state_lock_);

  // NetworkDeviceImpl protocol:
  zx_status_t NetworkDeviceImplInit(const network_device_ifc_protocol_t* iface);
  void NetworkDeviceImplStart(network_device_impl_start_callback callback, void* cookie);
  void NetworkDeviceImplStop(network_device_impl_stop_callback callback, void* cookie);
  void NetworkDeviceImplGetInfo(device_info_t* out_info);
  void NetworkDeviceImplQueueTx(const tx_buffer_t* buf_list, size_t buf_count);
  void NetworkDeviceImplQueueRxSpace(const rx_space_buffer_t* buf_list, size_t buf_count);
  void NetworkDeviceImplPrepareVmo(uint8_t vmo_id, zx::vmo vmo,
                                   network_device_impl_prepare_vmo_callback callback, void* cookie);
  void NetworkDeviceImplReleaseVmo(uint8_t vmo_id);
  void NetworkDeviceImplSetSnoop(bool snoop) { /* do nothing , only auto-snooping is allowed */
  }
  // NetworkPort protocol:
  void NetworkPortGetInfo(port_info_t* out_info);
  void NetworkPortGetStatus(port_status_t* out_status);
  void NetworkPortSetActive(bool active);
  void NetworkPortGetMac(mac_addr_protocol_t* out_mac_ifc);
  void NetworkPortRemoved() { /* do nothing, we never remove our port */
  }
  // MacAddr protocol:
  void MacAddrGetAddress(uint8_t* out_mac);
  void MacAddrGetFeatures(features_t* out_features);
  void MacAddrSetMode(mode_t mode, const uint8_t* multicast_macs_list, size_t multicast_macs_count);

  const char* tag() const override { return "virtio-net"; }

  uint16_t virtio_header_len() const { return virtio_hdr_len_; }

 private:
  friend class NetworkDeviceTests;
  uint16_t NegotiateHeaderLength();

  DISALLOW_COPY_ASSIGN_AND_MOVE(NetworkDevice);

  // Implementation of IrqRingUpdate; returns true if it should be called again.
  bool IrqRingUpdateInternal() __TA_EXCLUDES(state_lock_);
  port_status_t ReadStatus() const;

  // Mutexes to control concurrent access
  network::SharedLock state_lock_;
  std::mutex tx_lock_;
  std::mutex rx_lock_;

  // Virtqueues; see section 5.1.2 of the spec
  //
  // This driver doesn't currently support multi-queueing, automatic
  // steering, or the control virtqueue, so only a single queue is needed in
  // each direction.
  //
  // https://docs.oasis-open.org/virtio/virtio/v1.1/csprd01/virtio-v1.1-csprd01.html#x1-1960002
  Ring rx_ __TA_GUARDED(rx_lock_);
  Ring tx_ __TA_GUARDED(tx_lock_);

  struct Descriptor {
    uint32_t buffer_id;
    uint16_t ring_id;
  };
  class FifoQueue {
   public:
    void Push(Descriptor t) {
      ZX_ASSERT(count_ < data_.size());
      data_[wr_] = t;
      wr_ = (wr_ + 1) % data_.size();
      count_++;
    }
    Descriptor Pop() {
      ZX_ASSERT(count_ > 0);
      Descriptor t = data_[rd_];
      rd_ = (rd_ + 1) % data_.size();
      count_--;
      return t;
    }
    bool Empty() const { return count_ == 0; }

   private:
    std::array<Descriptor, kBacklog> data_;
    size_t wr_ = 0;
    size_t rd_ = 0;
    size_t count_ = 0;
  };
  FifoQueue tx_in_flight_ __TA_GUARDED(tx_lock_);
  FifoQueue rx_in_flight_ __TA_GUARDED(rx_lock_);

  fuchsia_net::wire::MacAddress mac_;
  uint16_t virtio_hdr_len_;

  ddk::NetworkDeviceIfcProtocolClient ifc_ __TA_GUARDED(state_lock_);
  VmoStore vmo_store_ __TA_GUARDED(state_lock_);
};

}  // namespace virtio

#endif  // SRC_CONNECTIVITY_ETHERNET_DRIVERS_VIRTIO_NETDEVICE_H_
