// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_DEVICE_VIRTIO_NET_GUEST_ETHERNET_H_
#define SRC_VIRTUALIZATION_BIN_VMM_DEVICE_VIRTIO_NET_GUEST_ETHERNET_H_

#include <fuchsia/hardware/ethernet/cpp/fidl.h>
#include <fuchsia/hardware/network/cpp/fidl.h>
#include <fuchsia/hardware/network/device/cpp/banjo.h>
#include <fuchsia/hardware/network/mac/cpp/banjo.h>
#include <fuchsia/net/virtualization/cpp/fidl.h>
#include <lib/async/cpp/wait.h>
#include <lib/fit/function.h>
#include <lib/fit/thread_checker.h>
#include <zircon/device/ethernet.h>

#include "src/connectivity/network/drivers/network-device/device/public/network_device.h"

// Interface for GuestEthernet to send a packet to the guest.
struct GuestEthernetDevice {
  // Send the given packet to the guest.
  //
  // The guest's function TxComplete will be called with `buffer_id` when
  // transmission has completed. The memory in [addr, addr + length) must
  // remain valid until the callback has been called.
  virtual void Receive(cpp20::span<const uint8_t> data, uint32_t buffer_id) = 0;

  // Notify the guest that the host is ready to receive packets.
  virtual void ReadyToSend() = 0;

  // Get the MAC address of the guest's ethernet.
  virtual fuchsia::hardware::ethernet::MacAddress GetMacAddress() = 0;
};

class GuestEthernet : public ddk::NetworkDeviceImplProtocol<GuestEthernet>,
                      ddk::MacAddrProtocol<GuestEthernet>,
                      ddk::NetworkPortProtocol<GuestEthernet> {
 public:
  // Port GuestEthernet uses for communication.
  static constexpr uint8_t kPortId = 0;

  explicit GuestEthernet(async_dispatcher_t* dispatcher, GuestEthernetDevice* device)
      : dispatcher_(dispatcher), device_(device) {}

  // Send the given ethernet frame to netstack.
  //
  // Returns ZX_OK on success, ZX_ERR_SHOULD_WAIT if no buffer space is
  // available, or other values on error.
  zx_status_t Send(void* data, uint16_t length);

  // Indicate that a packet sent via `GuestEthernetDevice::Receive` has
  // completed sending.
  void Complete(uint32_t buffer_id, zx_status_t status);

  // Methods implementing the `NetworkDevice` banjo protocol.
  zx_status_t NetworkDeviceImplInit(const network_device_ifc_protocol_t* iface);
  void NetworkDeviceImplStart(network_device_impl_start_callback callback, void* cookie);
  void NetworkDeviceImplStop(network_device_impl_stop_callback callback, void* cookie);
  void NetworkDeviceImplGetInfo(device_info_t* out_info);
  void NetworkDeviceImplQueueTx(const tx_buffer_t* buffers_list, size_t buffers_count);
  void NetworkDeviceImplQueueRxSpace(const rx_space_buffer_t* buffers_list, size_t buffers_count);
  void NetworkDeviceImplPrepareVmo(uint8_t vmo_id, zx::vmo vmo,
                                   network_device_impl_prepare_vmo_callback callback, void* cookie);
  void NetworkDeviceImplReleaseVmo(uint8_t vmo_id);
  void NetworkDeviceImplSetSnoop(bool snoop);

  // Methods implementing the `MacAddr` banjo protocol.
  void MacAddrGetAddress(uint8_t out_mac[MAC_SIZE]);
  void MacAddrGetFeatures(features_t* out_features);
  void MacAddrSetMode(mode_t mode, const uint8_t* multicast_macs_list, size_t multicast_macs_count);

  // Methods implementing the `NetworkPort` banjo protocol.
  void NetworkPortGetInfo(port_info_t* out_info);
  void NetworkPortGetStatus(port_status_t* out_status);
  void NetworkPortGetMac(mac_addr_protocol_t* out_mac_ifc);
  void NetworkPortSetActive(bool active) {}
  void NetworkPortRemoved() {}

  ddk::NetworkDeviceImplProtocolClient GetNetworkDeviceImplClient();

 private:
  enum class State {
    kStopped,       // Device is idle
    kStarted,       // Device has started
    kShuttingDown,  // Device is shutting down, waiting for outstanding tranmissions to complete
  };

  // Notify this device that transmission of the given packet has completed.
  void TxComplete(uint32_t buffer_id, zx_status_t status);

  // Notify netstack that the given buffer has been processed.
  //
  // A length of 0 can be used to indicate that the buffer was unused.
  void RxComplete(uint32_t buffer_id, size_t length);

  // If the device is in the ShuttingDown state and no packets are pending, finish
  // device shutdown.
  void FinishShutdownIfRequired() __TA_REQUIRES(mutex_);

  // Return a span of memory inside the VMO. Returns nullopt if the given
  // range is invalid.
  zx::result<cpp20::span<uint8_t>> GetIoRegion(uint8_t vmo_id, uint64_t offset, uint64_t length)
      __TA_REQUIRES(mutex_);

  std::mutex mutex_;

  async_dispatcher_t* dispatcher_;
  ddk::NetworkDeviceIfcProtocolClient parent_;
  GuestEthernetDevice* device_;

  // Device state.
  State state_ __TA_GUARDED(mutex_) = State::kStopped;
  uint32_t in_flight_tx_ __TA_GUARDED(mutex_);  // Number of in-flight packets being sent to guest.
  fit::function<void()> shutdown_complete_callback_
      __TA_GUARDED(mutex_);  // Callback called when transition
                             // from shutdown -> stop.

  // Memory shared with netstack.
  zx::vmo io_vmo_ __TA_GUARDED(mutex_);  // VMO shared with netstack for packet transfer.
  uint8_t* io_addr_ __TA_GUARDED(mutex_) = nullptr;     // Beginning of the IO region.
  size_t io_size_ __TA_GUARDED(mutex_) = 0;             // Length of the mapping, in bytes.
  std::optional<uint8_t> vmo_id_ __TA_GUARDED(mutex_);  // Netstack's identifier for the VMO.

  // Available buffers for sending packets to netstack.
  struct AvailableBuffer {
    uint32_t buffer_id;
    cpp20::span<uint8_t> region;
  };
  std::vector<AvailableBuffer> available_buffers_ __TA_GUARDED(mutex_);
};

#endif  // SRC_VIRTUALIZATION_BIN_VMM_DEVICE_VIRTIO_NET_GUEST_ETHERNET_H_
