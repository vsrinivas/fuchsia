// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/connectivity/wlan/drivers/lib/components/cpp/include/wlan/drivers/components/network_device.h"

#include <fidl/fuchsia.hardware.network/cpp/wire.h>
#include <lib/ddk/debug.h>
#include <lib/zx/vmar.h>

namespace {

void PopulateRxBuffer(rx_buffer_t& buffer, rx_buffer_part_t& buffer_part,
                      const wlan::drivers::components::Frame& frame) {
  buffer.data_count = 1;
  buffer.data_list = &buffer_part;
  buffer.meta.port = frame.PortId();
  buffer.meta.frame_type =
      static_cast<uint8_t>(::fuchsia_hardware_network::wire::FrameType::kEthernet);
  buffer_part.id = frame.BufferId();
  buffer_part.length = frame.Size();
  buffer_part.offset = frame.Headroom();
}

}  // anonymous namespace

namespace wlan::drivers::components {

NetworkDevice::Callbacks::~Callbacks() = default;

NetworkDevice::NetworkDevice(zx_device_t* parent, Callbacks* callbacks)
    : parent_(parent),
      callbacks_(callbacks),
      netdev_proto_{&this->network_device_impl_protocol_ops_, this} {}

NetworkDevice::~NetworkDevice() = default;

zx_status_t NetworkDevice::Init(const char* deviceName) { return AddNetworkDevice(deviceName); }

void NetworkDevice::Remove() { device_async_remove(device_); }

void NetworkDevice::Release() { callbacks_->NetDevRelease(); }

network_device_ifc_protocol_t NetworkDevice::NetDevIfcProto() const {
  network_device_ifc_protocol_t proto;
  netdev_ifc_.GetProto(&proto);
  return proto;
}

void NetworkDevice::CompleteRx(Frame&& frame) {
  if (!ShouldCompleteFrame(frame)) {
    return;
  }

  rx_buffer_t buffer = {};
  rx_buffer_part_t buffer_part = {};

  PopulateRxBuffer(buffer, buffer_part, frame);
  frame.ReleaseFromStorage();

  netdev_ifc_.CompleteRx(&buffer, 1u);
}

void NetworkDevice::CompleteRx(FrameContainer&& frames) {
  // Limit the number of buffers per call to CompleteRx. This allows us to avoid a memory allocation
  // here just to hold these buffers. This value was chosen because it is unlikely to be exceeded
  // in normal circumstances, thus avoiding multiple CompleteRx calls, while also being small enough
  // that we don't use up too much stack space.
  constexpr size_t kRxBuffersPerBatch = 256;
  rx_buffer_t buffers[kRxBuffersPerBatch];
  rx_buffer_part_t buffer_parts[std::size(buffers)];

  // Clear whatever buffers we're going to use
  const size_t buffers_to_fill = std::min(std::size(buffers), frames.size());
  memset(buffers, 0, buffers_to_fill * sizeof(buffers[0]));
  memset(buffer_parts, 0, buffers_to_fill * sizeof(buffer_parts[0]));

  for (auto frame = frames.begin(); frame != frames.end();) {
    size_t count = 0;
    for (; count < std::size(buffers) && frame != frames.end(); ++frame) {
      if (!ShouldCompleteFrame(*frame)) {
        continue;
      }
      PopulateRxBuffer(buffers[count], buffer_parts[count], *frame);
      frame->ReleaseFromStorage();
      ++count;
    }
    netdev_ifc_.CompleteRx(buffers, count);
  }
}

void NetworkDevice::CompleteTx(cpp20::span<Frame> frames, zx_status_t status) {
  // Limit the number of results per call to CompleteTx. This allows us to avoid a memory allocation
  // here just to hold these results. This value was chosen because it is unlikely to be exceeded
  // in normal circumstances, thus avoiding multiple CompleteTx calls, while also being small enough
  // that we don't use up too much stack space.
  constexpr size_t kTxBuffersPerBatch = 256;
  tx_result_t results[kTxBuffersPerBatch];

  // Clear whatever results we're going to use
  const size_t results_to_fill = std::min(std::size(results), frames.size());
  memset(results, 0, results_to_fill * sizeof(results[0]));

  for (auto frame = frames.begin(); frame != frames.end();) {
    size_t count = 0;
    for (; count < std::size(results) && frame != frames.end(); ++frame) {
      if (unlikely(vmo_addrs_[frame->VmoId()] == nullptr)) {
        // Do not complete TX for frames that don't belong to the network device. These could be
        // remnants from after a stop that should not be completed or frames used internally by
        // the driver that made their way to this method.
        continue;
      }
      results[count].id = frame->BufferId();
      results[count].status = status;
      ++count;
    }
    netdev_ifc_.CompleteTx(results, count);
  }
}

// NetworkDeviceImpl implementation
zx_status_t NetworkDevice::NetworkDeviceImplInit(const network_device_ifc_protocol_t* iface) {
  netdev_ifc_ = ::ddk::NetworkDeviceIfcProtocolClient(iface);
  return callbacks_->NetDevInit();
}

void NetworkDevice::NetworkDeviceImplStart(network_device_impl_start_callback callback,
                                           void* cookie) {
  std::lock_guard lock(started_mutex_);
  started_ = true;
  callbacks_->NetDevStart(Callbacks::StartTxn(callback, cookie));
}

void NetworkDevice::NetworkDeviceImplStop(network_device_impl_stop_callback callback,
                                          void* cookie) {
  std::lock_guard lock(started_mutex_);
  started_ = false;
  callbacks_->NetDevStop(Callbacks::StopTxn(callback, cookie));
}

void NetworkDevice::NetworkDeviceImplGetInfo(device_info_t* out_info) {
  memset(out_info, 0, sizeof(*out_info));
  callbacks_->NetDevGetInfo(out_info);
}

void NetworkDevice::NetworkDeviceImplQueueTx(const tx_buffer_t* buffers_list,
                                             size_t buffers_count) {
  std::lock_guard lock(started_mutex_);
  if (!started_) {
    // Do not queue TX on a device that is not started. This can happen when a device is being
    // stopped. For threading reasons the Network Device cannot hold locks when its calling into
    // our implementation so it's possible that it will attempt to queue TX after it has called
    // stop. These buffers will need to be completed with an error status.
    std::vector<tx_result_t> results(buffers_count);
    for (size_t i = 0; i < buffers_count; ++i) {
      results[i].id = buffers_list[i].id;
      results[i].status = ZX_ERR_UNAVAILABLE;
    }
    netdev_ifc_.CompleteTx(results.data(), results.size());
    return;
  }
  tx_frames_.reserve(buffers_count);
  for (size_t i = 0; i < buffers_count; ++i) {
    const tx_buffer_t& buffer = buffers_list[i];
    const buffer_region_t& region = buffer.data_list[0];
    tx_frames_.emplace_back(nullptr, region.vmo, region.offset, buffer.id,
                            vmo_addrs_[region.vmo] + region.offset, region.length,
                            buffer.meta.port);
    tx_frames_.back().ShrinkHead(buffer.head_length);
    tx_frames_.back().ShrinkTail(buffer.tail_length);
  }
  callbacks_->NetDevQueueTx(tx_frames_);
  tx_frames_.clear();
}

void NetworkDevice::NetworkDeviceImplQueueRxSpace(const rx_space_buffer_t* buffers_list,
                                                  size_t buffers_count) {
  callbacks_->NetDevQueueRxSpace(buffers_list, buffers_count, vmo_addrs_);
}

void NetworkDevice::NetworkDeviceImplPrepareVmo(uint8_t id, zx::vmo vmo,
                                                network_device_impl_prepare_vmo_callback callback,
                                                void* cookie) {
  const zx_status_t status = [&]() {
    uint64_t size = 0;
    zx_status_t status = vmo.get_size(&size);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to get VMO size on prepare: %s", zx_status_get_string(status));
      return status;
    }

    zx_vaddr_t addr = 0;
    status = zx::vmar::root_self()->map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0, size, &addr);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to map VMO on prepare: %s", zx_status_get_string(status));
      return status;
    }
    vmo_addrs_[id] = reinterpret_cast<uint8_t*>(addr);

    vmo_lengths_[id] = size;

    return callbacks_->NetDevPrepareVmo(id, std::move(vmo), vmo_addrs_[id], size);
  }();

  callback(cookie, status);
}

void NetworkDevice::NetworkDeviceImplReleaseVmo(uint8_t id) {
  callbacks_->NetDevReleaseVmo(id);
  zx_status_t status =
      zx::vmar::root_self()->unmap(reinterpret_cast<zx_vaddr_t>(vmo_addrs_[id]), vmo_lengths_[id]);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to unmap VMO on release: %s", zx_status_get_string(status));
  }
  vmo_addrs_[id] = nullptr;
  vmo_lengths_[id] = 0;
}

void NetworkDevice::NetworkDeviceImplSetSnoop(bool snoop) {
  callbacks_->NetDevSetSnoopEnabled(snoop);
}

zx_status_t NetworkDevice::AddNetworkDevice(const char* deviceName) {
  static zx_protocol_device_t network_device_ops = {
      .version = DEVICE_OPS_VERSION,
      .release = [](void* ctx) { static_cast<NetworkDevice*>(ctx)->Release(); },
  };
  device_add_args_t netDevArgs = {};
  netDevArgs.version = DEVICE_ADD_ARGS_VERSION;
  netDevArgs.name = deviceName;
  netDevArgs.ctx = this;
  netDevArgs.ops = &network_device_ops;
  netDevArgs.proto_id = ZX_PROTOCOL_NETWORK_DEVICE_IMPL;
  netDevArgs.proto_ops = netdev_proto_.ops;

  return device_add(parent_, &netDevArgs, &device_);
}

bool NetworkDevice::ShouldCompleteFrame(const Frame& frame) {
  if (unlikely(vmo_addrs_[frame.VmoId()] == nullptr)) {
    // Do not return frames with VMOs we are not aware of. This can happen if the frame
    // belongs to some internal VMO in the driver, by skipping it here we allow the driver
    // some relaxation in what it passes to us. It can also happen if we are attempting to
    // complete receives after a VMO has been released, at that point returning the frame
    // serves no purpose.
    return false;
  }
  return true;
}

}  // namespace wlan::drivers::components
