// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_DEVICE_VIRTIO_NET_RS_SRC_CPP_GUEST_ETHERNET_H_
#define SRC_VIRTUALIZATION_BIN_VMM_DEVICE_VIRTIO_NET_RS_SRC_CPP_GUEST_ETHERNET_H_

// Don't reorder this header to avoid conflicting implementations of MAX_PORTS.
// clang-format off
#include <fuchsia/net/virtualization/cpp/fidl.h>
// clang-format on

#include <fuchsia/hardware/network/device/cpp/banjo.h>
#include <fuchsia/hardware/network/mac/cpp/banjo.h>
#include <fuchsia/net/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/trace-provider/provider.h>
#include <zircon/types.h>

#include <virtio/net.h>

#include "src/connectivity/network/drivers/network-device/device/public/network_device.h"

class GuestEthernet : public ddk::NetworkDeviceImplProtocol<GuestEthernet>,
                      ddk::MacAddrProtocol<GuestEthernet>,
                      ddk::NetworkPortProtocol<GuestEthernet> {
 public:
  GuestEthernet()
      : loop_(&kAsyncLoopConfigAttachToCurrentThread),
        trace_provider_(loop_.dispatcher()),
        svc_(sys::ServiceDirectory::CreateFromNamespace()) {}
  ~GuestEthernet();

  // Starts the dispatch loop on a new thread.
  zx_status_t StartDispatchLoop();

  // Initializes this guest ethernet object by parsing the Rust provided MAC address, preparing
  // callbacks, and registering it the netstack. This will be invoked by the Rust thread, and
  // scheduled on the C++ dispatch loop.
  //
  // Returns ZX_OK if it was successfully scheduled, and sends ZX_OK via set_status_ when finished.
  zx_status_t Initialize(const void* rust_guest_ethernet, const uint8_t* mac, size_t mac_len,
                         bool enable_bridge);

  // Send the packet to the netstack, returning ZX_OK if the packet was sent successfully, and
  // ZX_ERR_SHOULD_WAIT if no buffer space is available and the device should retry later.
  zx_status_t Send(const void* data, uint16_t length);

  // Indicate that a packet has been successfully sent to the guest and that the memory can be
  // reclaimed.
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
  void MacAddrGetAddress(uint8_t out_mac[VIRTIO_ETH_MAC_SIZE]);
  void MacAddrGetFeatures(features_t* out_features);
  void MacAddrSetMode(mode_t mode, const uint8_t* multicast_macs_list, size_t multicast_macs_count);

  // Methods implementing the `NetworkPort` banjo protocol.
  void NetworkPortGetInfo(port_info_t* out_info);
  void NetworkPortGetStatus(port_status_t* out_status);
  void NetworkPortGetMac(mac_addr_protocol_t* out_mac_ifc);
  void NetworkPortSetActive(bool active) {}
  void NetworkPortRemoved() {}

 private:
  enum class State {
    kStopped,       // Device is idle.
    kStarted,       // Device has started.
    kShuttingDown,  // Device is shutting down, waiting for outstanding transmissions to complete.
  };

  // Notify the netstack that the given buffer has been used. A length of 0 can be sent to indicate
  // that the buffer was unused.
  //
  // Note that this is tx from the perspective of the guest.
  void TxComplete(uint32_t buffer_id, size_t length);

  // Notify the netstack that the buffer has been sent to the guest (or failed, depending on the
  // status). As soon as this function is invoked, the netstack is free to reuse the underlying
  // buffer memory.
  //
  // Note that this is rx from the perspective of the guest.
  void RxComplete(uint32_t buffer_id, zx_status_t status);

  // Register this guest ethernet object with the netstack.
  zx_status_t CreateGuestInterface(bool enable_bridge);

  // Binds this class to the banjo protocol.
  ddk::NetworkDeviceImplProtocolClient GetNetworkDeviceImplClient();

  // If in state kShuttingDown with no in flight RX to the guest, this will invoke the shutdown
  // complete callback.
  void FinishShutdownIfRequired() __TA_REQUIRES(mutex_);

  // Return a span of memory inside the VMO. Returns nullopt if the given range is invalid.
  zx::result<cpp20::span<uint8_t>> GetIoRegion(uint8_t vmo_id, uint64_t offset, uint64_t length)
      __TA_REQUIRES(mutex_);

  std::mutex mutex_;
  ddk::NetworkDeviceIfcProtocolClient parent_;

  // Device state.
  State state_ __TA_GUARDED(mutex_) = State::kStopped;
  uint32_t in_flight_rx_ __TA_GUARDED(mutex_);  // Packets sent to the guest but uncompleted.
  fit::function<void()> shutdown_complete_callback_ __TA_GUARDED(mutex_);

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

  async::Loop loop_;
  trace::TraceProviderWithFdio trace_provider_;
  std::shared_ptr<sys::ServiceDirectory> svc_;

  uint8_t mac_address_[VIRTIO_ETH_MAC_SIZE];

  ::fuchsia::net::virtualization::ControlPtr netstack_;
  ::fuchsia::net::virtualization::NetworkPtr network_;
  ::fuchsia::net::virtualization::InterfacePtr interface_registration_;
  ::std::unique_ptr<network::NetworkDeviceInterface> device_interface_;

  fit::function<void()> ready_for_guest_tx_;
  fit::function<void(zx_status_t)> set_status_;
  fit::function<void(uint8_t*, size_t, uint32_t)> send_guest_rx_;
};

#endif  // SRC_VIRTUALIZATION_BIN_VMM_DEVICE_VIRTIO_NET_RS_SRC_CPP_GUEST_ETHERNET_H_
