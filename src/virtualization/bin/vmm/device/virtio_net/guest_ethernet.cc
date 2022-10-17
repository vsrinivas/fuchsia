// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "guest_ethernet.h"

#include <fidl/fuchsia.hardware.network/cpp/wire_types.h>
#include <fuchsia/hardware/network/device/cpp/banjo.h>
#include <fuchsia/hardware/network/mac/cpp/banjo.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/stdcompat/span.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/fifo.h>

namespace {

using fuchsia::hardware::network::FrameType;

// Maximum Transmission Unit (MTU): the maximum supported size of an incoming/outgoing frame.
constexpr uint32_t kMtu = 1500;

// Maximum number of in-flight packets.
constexpr uint16_t kMaxTxDepth = 128;
constexpr uint16_t kMaxRxDepth = 128;

// Ensure the given buffer can be supported by virtio-net.
bool IsTxBufferSupported(const tx_buffer& buffer) {
  // Ensure no padding on the head/tail.
  if (unlikely(buffer.head_length != 0)) {
    FX_LOGS_FIRST_N(WARNING, 10) << "Packet from host contained invalid head length: "
                                 << buffer.head_length;
    return false;
  }
  if (unlikely(buffer.tail_length != 0)) {
    FX_LOGS_FIRST_N(WARNING, 10) << "Packet from host contained invalid tail length: "
                                 << buffer.tail_length;
    return false;
  }

  // Ensure the default port is being used.
  if (unlikely(buffer.meta.port != GuestEthernet::kPortId)) {
    FX_LOGS_FIRST_N(WARNING, 10) << "Packet from host contained invalid device port: "
                                 << buffer.meta.port;
    return false;
  }

  // Ensure the buffer contains a standard ethernet frame.
  if (unlikely(static_cast<FrameType>(buffer.meta.frame_type) != FrameType::ETHERNET)) {
    FX_LOGS_FIRST_N(WARNING, 10) << "Packet from host contained unsupported type: "
                                 << buffer.meta.frame_type;
    return false;
  }

  // We currently only support a single data buffer.
  if (unlikely(buffer.data_count != 1)) {
    FX_LOGS_FIRST_N(WARNING, 10) << "Packet from host contained multiple data buffers";
    return false;
  }

  return true;
}

}  // namespace

zx_status_t GuestEthernet::Send(void* data, uint16_t length) {
  std::lock_guard guard(mutex_);

  if (!io_vmo_) {
    FX_LOGS(WARNING) << "Send called before IO buffer was set up";
    return ZX_ERR_BAD_STATE;
  }

  if (available_buffers_.empty()) {
    return ZX_ERR_SHOULD_WAIT;
  }

  // Get a buffer.
  AvailableBuffer buffer = available_buffers_.back();
  available_buffers_.pop_back();

  // Ensure the packet will fit in the buffer.
  if (length > buffer.region.size()) {
    FX_LOGS(WARNING) << "Incoming packet of size " << length
                     << " could not be stored in buffer of size " << buffer.region.size();
    // Drop the packet.
    return ZX_ERR_NO_RESOURCES;
  }

  // Copy data from the virtio ring to memory shared with netstack.
  memcpy(buffer.region.data(), data, length);

  // Return the buffer to our parent device.
  RxComplete(buffer.buffer_id, length);

  return ZX_OK;
}

void GuestEthernet::Complete(uint32_t buffer_id, zx_status_t status) {
  std::lock_guard guard(mutex_);
  TxComplete(buffer_id, status);
  FX_DCHECK(in_flight_tx_ > 0);
  in_flight_tx_--;

  // Stop the device if we are shutting down, and no more packets are pending.
  FinishShutdownIfRequired();
}

zx_status_t GuestEthernet::NetworkDeviceImplInit(const network_device_ifc_protocol_t* iface) {
  FX_CHECK(!parent_.is_valid()) << "NetworkDeviceImplInit called multiple times";
  parent_ = ddk::NetworkDeviceIfcProtocolClient(iface);

  // Create port.
  parent_.AddPort(kPortId, this, &network_port_protocol_ops_);

  // Inform our parent that the port is active.
  port_status_t port_status;
  NetworkPortGetStatus(&port_status);
  parent_.PortStatusChanged(kPortId, &port_status);

  return ZX_OK;
}

void GuestEthernet::NetworkDeviceImplStart(network_device_impl_start_callback callback,
                                           void* cookie) {
  zx_status_t result = ZX_OK;
  {
    std::lock_guard guard(mutex_);
    if (state_ == State::kStopped) {
      state_ = State::kStarted;
    } else {
      result = ZX_ERR_BAD_STATE;
    }
  }
  callback(cookie, result);
}

void GuestEthernet::NetworkDeviceImplStop(network_device_impl_stop_callback callback,
                                          void* cookie) {
  std::lock_guard guard(mutex_);
  FX_CHECK(state_ == State::kStarted) << "Attempted to stop device in bad state.";

  // Return any available RX buffer.
  while (!available_buffers_.empty()) {
    RxComplete(available_buffers_.back().buffer_id, /*length=*/0);
    available_buffers_.pop_back();
  }

  // Wait for in-flight packets to be completed.
  state_ = State::kShuttingDown;
  shutdown_complete_callback_ = [cookie, callback]() { callback(cookie); };

  // If no packets are in-flight, shut down the device.
  FinishShutdownIfRequired();
}

void GuestEthernet::FinishShutdownIfRequired() {
  if (state_ == State::kShuttingDown && in_flight_tx_ == 0) {
    async::PostTask(dispatcher_, std::move(shutdown_complete_callback_));
  }
}

void GuestEthernet::NetworkDeviceImplGetInfo(device_info_t* out_info) {
  *out_info = {
      // Allow at most kMaxTxDepth/kMaxRxDepth buffers in flight to TX/RX,
      // respectively.
      .tx_depth = kMaxTxDepth,
      .rx_depth = kMaxRxDepth,

      // Netstack should try to refresh our available RX buffers when they get
      // to 50% of MaxRxDepth.
      .rx_threshold = kMaxRxDepth / 2,

      // We only support buffers with 1 memory region (i.e., no scatter/gather).
      .max_buffer_parts = 1,

      // Buffers must be aligned to sizeof(uint64_t).
      .buffer_alignment = sizeof(uint64_t),

      // Require that all RX buffers are at least the size of our MTU.
      .min_rx_buffer_length = kMtu,
  };
}

// Enqueues a list of buffers for transmission on the network device.
void GuestEthernet::NetworkDeviceImplQueueTx(const tx_buffer_t* buffers_list,
                                             size_t buffers_count) {
  std::lock_guard guard(mutex_);

  for (size_t i = 0; i < buffers_count; ++i) {
    const tx_buffer& buffer = buffers_list[i];

    // Reject transactions if we are not running.
    if (state_ != State::kStarted) {
      TxComplete(buffer.id, ZX_ERR_UNAVAILABLE);
      continue;
    }

    // Ignore unsupported buffers.
    if (!IsTxBufferSupported(buffer)) {
      TxComplete(buffer.id, ZX_ERR_NOT_SUPPORTED);
      continue;
    }

    // Get the data payload.
    FX_DCHECK(buffer.data_count == 1);  // verified in IsTxBufferSupported
    const buffer_region& region = buffer.data_list[0];

    // Get the caller-specified region.
    zx::result<cpp20::span<uint8_t>> memory_region =
        GetIoRegion(region.vmo, region.offset, region.length);
    if (memory_region.is_error()) {
      TxComplete(buffer.id, memory_region.error_value());
      continue;
    }

    // Initiate transfer of memory to the guest.
    async::PostTask(dispatcher_, [this, id = buffer.id, memory_region = memory_region.value()] {
      device_->Receive(memory_region, id);
    });
    in_flight_tx_++;
  }
}

void GuestEthernet::NetworkDeviceImplQueueRxSpace(const rx_space_buffer_t* buffers_list,
                                                  size_t buffers_count) {
  std::lock_guard guard(mutex_);

  // If we previously ran out of buffers, notify the guest that new ones are available.
  bool need_notify = available_buffers_.empty();

  for (size_t i = 0; i < buffers_count; i++) {
    const rx_space_buffer_t& buffer = buffers_list[i];

    // Ensure the specified region is valid.
    zx::result<cpp20::span<uint8_t>> region =
        GetIoRegion(/*vmo_id=*/buffer.region.vmo, /*offset=*/buffer.region.offset,
                    /*length=*/buffer.region.length);
    if (region.is_error()) {
      // Return the buffer unused.
      RxComplete(buffer.id, /*length=*/0);
      continue;
    }

    // Record the buffer.
    available_buffers_.push_back({.buffer_id = buffer.id, .region = *region});
  }

  if (need_notify && !available_buffers_.empty()) {
    async::PostTask(dispatcher_, [this]() { device_->ReadyToSend(); });
  }
}

void GuestEthernet::NetworkDeviceImplPrepareVmo(uint8_t vmo_id, zx::vmo vmo,
                                                network_device_impl_prepare_vmo_callback callback,
                                                void* cookie) {
  std::lock_guard guard(mutex_);

  // Ensure another VMO hasn't already been mapped.
  if (io_vmo_.is_valid()) {
    FX_LOGS(INFO) << "Attempted to bind multiple VMOs";
    async::PostTask(dispatcher_, [callback, cookie]() { callback(cookie, ZX_ERR_NO_RESOURCES); });
    return;
  }

  // Get the VMO's size.
  uint64_t vmo_size;
  zx_status_t status = vmo.get_size(&vmo_size);
  if (status != ZX_OK) {
    FX_PLOGS(INFO, status) << "Failed to get VMO size";
    async::PostTask(dispatcher_, [callback, cookie, status]() { callback(cookie, status); });
    return;
  }

  // Map in the VMO.
  zx_vaddr_t mapped_address;
  status =
      zx::vmar::root_self()->map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_REQUIRE_NON_RESIZABLE,
                                 0, vmo, 0, vmo_size, &mapped_address);
  if (status != ZX_OK) {
    FX_PLOGS(INFO, status) << "Failed to map IO buffer";
    async::PostTask(dispatcher_, [callback, cookie, status]() { callback(cookie, status); });
    return;
  }

  vmo_id_ = vmo_id;
  io_addr_ = reinterpret_cast<uint8_t*>(mapped_address);
  io_vmo_ = std::move(vmo);
  io_size_ = vmo_size;
  async::PostTask(dispatcher_, [callback, cookie]() { callback(cookie, ZX_OK); });
}

void GuestEthernet::NetworkDeviceImplReleaseVmo(uint8_t vmo_id) {
  std::lock_guard guard(mutex_);

  // The NetworkDevice protocol states "`ReleaseVmo` is guaranteed to only be
  // called when the implementation holds no buffers that reference that `id`."
  FX_CHECK(io_vmo_.is_valid() && vmo_id != io_vmo_);
  FX_CHECK(available_buffers_.empty());

  // Unmap the VMO.
  zx_status_t status =
      zx::vmar::root_self()->unmap(reinterpret_cast<uintptr_t>(io_addr_), io_size_);
  FX_CHECK(status == ZX_OK) << "Could not unmap VMO: " << zx_status_get_string(status);

  // Reset state.
  io_vmo_.reset();
  vmo_id_.reset();
  io_addr_ = nullptr;
  io_size_ = 0;
}

void GuestEthernet::NetworkDeviceImplSetSnoop(bool snoop) {
  if (snoop) {
    FX_LOGS(WARNING) << "Request to enable snooping ignored: Snooping is unsupported";
  }
}

void GuestEthernet::TxComplete(uint32_t buffer_id, zx_status_t status) {
  async::PostTask(dispatcher_, [this, buffer_id, status]() {
    tx_result result = {
        .id = buffer_id,
        .status = status,
    };
    parent_.CompleteTx(&result, /*tx_count=*/1);
  });
}

void GuestEthernet::RxComplete(uint32_t buffer_id, size_t length) {
  FX_DCHECK(length < UINT32_MAX);
  async::PostTask(dispatcher_, [this, buffer_id, length = static_cast<uint32_t>(length)]() {
    rx_buffer_part part = {
        .id = buffer_id,
        .offset = 0,
        .length = static_cast<uint32_t>(length),
    };
    rx_buffer rx_info = {
        .meta =
            {
                .port = kPortId,
                .frame_type = static_cast<uint8_t>(FrameType::ETHERNET),
            },
        .data_list = &part,
        .data_count = 1,
    };
    parent_.CompleteRx(&rx_info, /*rx_count=*/1);
  });
}

zx::result<cpp20::span<uint8_t>> GuestEthernet::GetIoRegion(uint8_t vmo_id, uint64_t offset,
                                                            uint64_t length) {
  // Ensure the VMO ID matches what we have mapped in.
  if (vmo_id != vmo_id_) {
    return zx::error(ZX_ERR_NOT_FOUND);
  }

  // Ensure the child range does not overflow.
  uint64_t end;  // end of the range, exclusive
  if (add_overflow(offset, length, &end)) {
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }

  // Ensure the range is within the IO region.
  if (end > io_size_) {
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }

  return zx::success(cpp20::span<uint8_t>(io_addr_ + offset, length));
}

void GuestEthernet::MacAddrGetAddress(uint8_t out_mac[MAC_SIZE]) {
  fuchsia::hardware::ethernet::MacAddress mac = device_->GetMacAddress();
  std::copy(mac.octets.begin(), mac.octets.end(), out_mac);
}

void GuestEthernet::MacAddrGetFeatures(features_t* out_features) {
  *out_features = {
      // We don't support multicast filtering.
      .multicast_filter_count = 0,

      // We don't perform any filtering.
      .supported_modes = MODE_PROMISCUOUS,
  };
}

void GuestEthernet::MacAddrSetMode(mode_t mode, const uint8_t* multicast_macs_list,
                                   size_t multicast_macs_count) {
  FX_LOGS(WARNING) << "MacAddrSetMode not implemented";
}

void GuestEthernet::NetworkPortGetInfo(port_info_t* out_info) {
  static constexpr std::array<uint8_t, 1> kRxTypes = {
      static_cast<uint8_t>(fuchsia_hardware_network::wire::FrameType::kEthernet),
  };
  static const std::array<tx_support, 1> kTxTypes = {
      tx_support{
          .type = static_cast<uint8_t>(fuchsia_hardware_network::wire::FrameType::kEthernet),
          .features = static_cast<uint32_t>(fuchsia_hardware_network::wire::EthernetFeatures::kRaw),
      },
  };

  // Advertise we are a virtual port implementing support for TX/RX of raw
  // ethernet frames.
  *out_info = {
      .port_class = static_cast<uint8_t>(fuchsia_hardware_network::wire::DeviceClass::kVirtual),
      .rx_types_list = kRxTypes.data(),
      .rx_types_count = kRxTypes.size(),
      .tx_types_list = kTxTypes.data(),
      .tx_types_count = kTxTypes.size(),
  };
}

void GuestEthernet::NetworkPortGetStatus(port_status_t* out_status) {
  *out_status = {
      // Port's maximum transmission unit, in bytes.
      .mtu = kMtu,

      // Port status flags.
      //
      // Status flags, as defined in [`fuchsia.hardware.network/Status`].
      .flags = static_cast<uint32_t>(fuchsia_hardware_network::wire::StatusFlags::kOnline),
  };
}

void GuestEthernet::NetworkPortGetMac(mac_addr_protocol_t* out_mac_ifc) {
  *out_mac_ifc = {
      .ops = &mac_addr_protocol_ops_,
      .ctx = this,
  };
}

ddk::NetworkDeviceImplProtocolClient GuestEthernet::GetNetworkDeviceImplClient() {
  network_device_impl_protocol_t proto = {
      .ops = &network_device_impl_protocol_ops_,
      .ctx = this,
  };
  return ddk::NetworkDeviceImplProtocolClient(&proto);
}
