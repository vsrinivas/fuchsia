// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device_adapter.h"

#include <lib/sync/completion.h>
#include <lib/syslog/global.h>
#include <zircon/status.h>

#include <fbl/auto_lock.h>

namespace network {
namespace tun {

zx::status<std::unique_ptr<DeviceAdapter>> DeviceAdapter::Create(
    async_dispatcher_t* dispatcher, DeviceAdapterParent* parent, bool online,
    std::optional<fuchsia_net::wire::MacAddress> mac) {
  fbl::AllocChecker ac;
  std::unique_ptr<DeviceAdapter> adapter(new (&ac) DeviceAdapter(parent, online));
  if (!ac.check()) {
    return zx::error(ZX_ERR_NO_MEMORY);
  }
  network_device_impl_protocol_t proto = {
      .ops = &adapter->network_device_impl_protocol_ops_,
      .ctx = adapter.get(),
  };

  // NB: Mac must be created first so it is set when kPort0 is created.
  if (mac.has_value()) {
    zx::status mac_adapter = MacAdapter::Create(parent, mac.value(), false);
    if (mac_adapter.is_error()) {
      return mac_adapter.take_error();
    }
    adapter->mac_ = std::move(mac_adapter.value());
  }

  zx::status device = NetworkDeviceInterface::Create(
      dispatcher, ddk::NetworkDeviceImplProtocolClient(&proto), "network-tun");
  if (device.is_error()) {
    return device.take_error();
  }
  adapter->device_ = std::move(device.value());

  return zx::ok(std::move(adapter));
}

zx_status_t DeviceAdapter::Bind(fidl::ServerEnd<netdev::Device> req) {
  return device_->Bind(std::move(req));
}

zx_status_t DeviceAdapter::NetworkDeviceImplInit(const network_device_ifc_protocol_t* iface) {
  device_iface_ = ddk::NetworkDeviceIfcProtocolClient(iface);
  device_iface_.AddPort(kPort0, this, &network_port_protocol_ops_);
  return ZX_OK;
}

void DeviceAdapter::NetworkDeviceImplStart(network_device_impl_start_callback callback,
                                           void* cookie) {
  bool tx_valid;
  {
    fbl::AutoLock lock(&state_lock_);
    has_sessions_ = true;
    tx_valid = online_;
  }
  {
    fbl::AutoLock lock(&rx_lock_);
    rx_available_ = true;
  }
  {
    fbl::AutoLock lock(&tx_lock_);
    tx_available_ = tx_valid;
  }
  parent_->OnHasSessionsChanged(this);
  callback(cookie);
}

void DeviceAdapter::NetworkDeviceImplStop(network_device_impl_stop_callback callback,
                                          void* cookie) {
  {
    fbl::AutoLock lock(&state_lock_);
    has_sessions_ = false;
  }
  {
    // Return all rx buffers.
    fbl::AutoLock lock(&rx_lock_);
    rx_available_ = false;
    while (!rx_buffers_.empty()) {
      const rx_space_buffer_t& buffer = rx_buffers_.front();
      rx_buffer_part_t part = {
          .id = buffer.id,
          .length = 0,
      };
      rx_buffer_t return_buffer = {
          .data_list = &part,
          .data_count = 1,
      };
      device_iface_.CompleteRx(&return_buffer, 1);
      rx_buffers_.pop();
    }
  }
  {
    // Return all tx buffers.
    fbl::AutoLock lock(&tx_lock_);
    tx_available_ = false;
    while (!tx_buffers_.empty()) {
      const TxBuffer& buffer = tx_buffers_.front();
      tx_result_t result = {
          .id = buffer.id(),
          .status = ZX_ERR_UNAVAILABLE,
      };
      device_iface_.CompleteTx(&result, 1);
      tx_buffers_.pop();
    }
  }
  parent_->OnHasSessionsChanged(this);
  callback(cookie);
}

void DeviceAdapter::NetworkDeviceImplGetInfo(device_info_t* out_info) { *out_info = device_info_; }

void DeviceAdapter::NetworkDeviceImplQueueTx(const tx_buffer_t* buf_list, size_t buf_count) {
  {
    fbl::AutoLock tx_lock(&tx_lock_);
    fbl::Span buffers(buf_list, buf_count);
    if (!tx_available_) {
      FX_VLOGF(1, "tun", "Discarding %d tx buffers, tx queue is invalid", tx_available_);
      for (const tx_buffer_t& b : buffers) {
        EnqueueTx(b.id, ZX_ERR_UNAVAILABLE);
      }
      CommitTx();
      return;
    }
    for (const tx_buffer_t& b : buffers) {
      tx_buffers_.emplace(vmos_.MakeTxBuffer(b, parent_->config().report_metadata));
      buf_list++;
    }
  }
  parent_->OnTxAvail(this);
}

void DeviceAdapter::NetworkDeviceImplQueueRxSpace(const rx_space_buffer_t* buf_list,
                                                  size_t buf_count) {
  bool has_buffers;
  {
    fbl::AutoLock lock(&rx_lock_);
    fbl::Span buffers(buf_list, buf_count);
    if (!rx_available_) {
      for (const rx_space_buffer_t& space : buffers) {
        rx_buffer_part_t part = {
            .id = space.id,
            .length = 0,
        };
        rx_buffer_t buffer = {
            .data_list = &part,
            .data_count = 1,
        };
        device_iface_.CompleteRx(&buffer, 1);
      }
      return;
    }
    for (const rx_space_buffer_t& space : buffers) {
      rx_buffers_.push(space);
    }
    has_buffers = !rx_buffers_.empty();
  }
  if (has_buffers) {
    parent_->OnRxAvail(this);
  }
}

void DeviceAdapter::NetworkDeviceImplPrepareVmo(uint8_t vmo_id, zx::vmo vmo) {
  zx_status_t status = vmos_.RegisterVmo(vmo_id, std::move(vmo));
  if (status != ZX_OK) {
    FX_LOGF(ERROR, "tun", "DeviceAdapter failed to register vmo: %s", zx_status_get_string(status));
  }
}

void DeviceAdapter::NetworkDeviceImplReleaseVmo(uint8_t vmo_id) {
  zx_status_t status = vmos_.UnregisterVmo(vmo_id);
  if (status != ZX_OK) {
    FX_LOGF(ERROR, "tun", "DeviceAdapter failed to unregister vmo: %s",
            zx_status_get_string(status));
  }
}

void DeviceAdapter::NetworkPortGetInfo(port_info_t* out_info) { *out_info = port_info_; }
void DeviceAdapter::NetworkPortGetStatus(port_status_t* out_status) {
  fbl::AutoLock lock(&state_lock_);
  *out_status = {
      .mtu = parent_->config().mtu,
      .flags =
          online_ ? static_cast<uint32_t>(fuchsia_hardware_network::wire::StatusFlags::kOnline) : 0,
  };
}
void DeviceAdapter::NetworkPortSetActive(bool active) {}
void DeviceAdapter::NetworkPortGetMac(mac_addr_protocol_t* out_mac_ifc) {
  if (mac_) {
    *out_mac_ifc = mac_->proto();
  } else {
    *out_mac_ifc = {};
  }
}
void DeviceAdapter::NetworkPortRemoved() {}

void DeviceAdapter::SetOnline(bool online) {
  port_status_t new_status;
  {
    fbl::AutoLock lock(&state_lock_);
    if (online == online_) {
      return;
    }
    FX_VLOGF(1, "tun", "DeviceAdapter: SetOnline: %d", online);
    online_ = online;
    new_status.mtu = parent_->config().mtu;
    new_status.flags =
        online_ ? static_cast<uint32_t>(fuchsia_hardware_network::wire::StatusFlags::kOnline) : 0;

    {
      fbl::AutoLock tx_lock(&tx_lock_);
      tx_available_ = online_ && has_sessions_;
      // If going offline, discard all pending tx buffers.
      if (!tx_available_) {
        while (!tx_buffers_.empty()) {
          EnqueueTx(tx_buffers_.front().id(), ZX_ERR_UNAVAILABLE);
          tx_buffers_.pop();
        }
        CommitTx();
      }
    }
  }
  // TODO(https://fxbug.dev/64310): Don't hard-coded port 0 once tun can have more ports.
  device_iface_.PortStatusChanged(kPort0, &new_status);
}

bool DeviceAdapter::HasSession() {
  fbl::AutoLock lock(&state_lock_);
  return has_sessions_;
}

bool DeviceAdapter::TryGetTxBuffer(fit::callback<zx_status_t(TxBuffer&, size_t)> callback) {
  uint32_t id;

  fbl::AutoLock lock(&tx_lock_);
  if (tx_buffers_.empty()) {
    return false;
  }
  auto& buff = tx_buffers_.front();
  auto avail = tx_buffers_.size() - 1;
  zx_status_t status = callback(buff, avail);
  id = buff.id();
  tx_buffers_.pop();

  EnqueueTx(id, status);
  CommitTx();
  return true;
}

zx::status<size_t> DeviceAdapter::WriteRxFrame(
    fuchsia_hardware_network::wire::FrameType frame_type, const uint8_t* data, size_t count,
    const std::optional<fuchsia_net_tun::wire::FrameMetadata>& meta) {
  {
    // can't write if device is offline
    fbl::AutoLock lock(&state_lock_);
    if (!online_) {
      return zx::error(ZX_ERR_BAD_STATE);
    }
  }

  if (count > parent_->config().mtu) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  fbl::AutoLock lock(&rx_lock_);
  if (rx_buffers_.empty()) {
    return zx::error(ZX_ERR_SHOULD_WAIT);
  }
  zx::status alloc = AllocRxSpace(count);
  if (alloc.is_error()) {
    return alloc.take_error();
  }
  RxBuffer buffer = std::move(alloc.value());
  if (zx_status_t status = buffer.Write(data, count); status != ZX_OK) {
    ReclaimRxSpace(std::move(buffer));
    return zx::error(status);
  }
  EnqueueRx(frame_type, std::move(buffer), count, meta);
  CommitRx();

  return zx::ok(rx_buffers_.size());
}

zx::status<size_t> DeviceAdapter::WriteRxFrame(
    fuchsia_hardware_network::wire::FrameType frame_type, const fidl::VectorView<uint8_t>& data,
    const std::optional<fuchsia_net_tun::wire::FrameMetadata>& meta) {
  return WriteRxFrame(frame_type, data.data(), data.count(), meta);
}

zx::status<size_t> DeviceAdapter::WriteRxFrame(
    fuchsia_hardware_network::wire::FrameType frame_type, const std::vector<uint8_t>& data,
    const std::optional<fuchsia_net_tun::wire::FrameMetadata>& meta) {
  return WriteRxFrame(frame_type, data.data(), data.size(), meta);
}

void DeviceAdapter::CopyTo(DeviceAdapter* other, bool return_failed_buffers) {
  fbl::AutoLock tx_lock(&tx_lock_);
  fbl::AutoLock rx_lock(&other->rx_lock_);

  while (!tx_buffers_.empty()) {
    TxBuffer& tx_buff = tx_buffers_.front();
    zx::status alloc_rx = other->AllocRxSpace(tx_buff.length());
    if (alloc_rx.is_error()) {
      if (!return_failed_buffers) {
        // stop once we run out of rx buffers to copy to
        FX_VLOG(1, "tun", "DeviceAdapter:CopyTo: no more rx buffers");
        break;
      }
      EnqueueTx(tx_buff.id(), ZX_ERR_NO_RESOURCES);
      tx_buffers_.pop();
      continue;
    }
    RxBuffer rx_buff = std::move(alloc_rx.value());
    zx::status status = rx_buff.CopyFrom(tx_buff);
    if (status.is_error()) {
      FX_LOGF(ERROR, "tun", "DeviceAdapter:CopyTo: Failed to copy buffer: %s",
              status.status_string());
      EnqueueTx(tx_buff.id(), status.status_value());
      other->ReclaimRxSpace(std::move(rx_buff));
    } else {
      size_t length = status.value();
      // Enqueue the data to be returned in other, and enqueue the complete tx in self.
      std::optional meta = tx_buff.TakeMetadata();
      if (meta.has_value()) {
        meta->flags = 0;
      }
      other->EnqueueRx(tx_buff.frame_type(), std::move(rx_buff), length, meta);
      EnqueueTx(tx_buff.id(), ZX_OK);
    }
    tx_buffers_.pop();
  }
  CommitTx();
  other->CommitRx();
}

void DeviceAdapter::Teardown(fit::function<void()> callback) {
  device_->Teardown([this, cb = std::move(callback)]() mutable {
    if (mac_) {
      mac_->Teardown(std::move(cb));
    } else {
      cb();
    }
  });
}

void DeviceAdapter::TeardownSync() {
  sync_completion_t completion;
  Teardown([&completion]() { sync_completion_signal(&completion); });
  sync_completion_wait_deadline(&completion, ZX_TIME_INFINITE);
}

void DeviceAdapter::EnqueueRx(fuchsia_hardware_network::wire::FrameType frame_type, RxBuffer buffer,
                              size_t length,
                              const std::optional<fuchsia_net_tun::wire::FrameMetadata>& meta)
    __TA_REQUIRES(rx_lock_) {
  // Written length must always fit the buffer.
  ZX_DEBUG_ASSERT(buffer.length() >= length);
  size_t old_rx_parts_count = return_rx_parts_count_;
  buffer.WithReturn(length, [this](const rx_buffer_part_t& part) {
    // WithReturn is called inline.
    []() __TA_ASSERT(rx_lock_) {}();

    // We should not be producing zero-length parts.
    ZX_DEBUG_ASSERT(part.length != 0);
    // Can't accumulate more parts than can fit in our array.
    ZX_ASSERT(return_rx_parts_count_ <= return_rx_parts_.size());
    return_rx_parts_[return_rx_parts_count_++] = part;
  });
  rx_buffer_t& ret = return_rx_list_.emplace_back(rx_buffer_t{
      .meta =
          {
              .port = kPort0,
              .info_type = static_cast<uint32_t>(fuchsia_hardware_network::wire::InfoType::kNoInfo),
              .frame_type = static_cast<uint8_t>(frame_type),
          },
      .data_list = &return_rx_parts_[old_rx_parts_count],
      .data_count = return_rx_parts_count_ - old_rx_parts_count,
  });
  if (meta) {
    ret.meta.flags = meta->flags;
    ret.meta.info_type = static_cast<uint32_t>(meta->info_type);
    if (meta->info_type != fuchsia_hardware_network::wire::InfoType::kNoInfo) {
      FX_LOGF(WARNING, "tun", "Unrecognized info type %d", ret.meta.info_type);
    }
  }
}

void DeviceAdapter::CommitRx() {
  if (!return_rx_list_.empty()) {
    device_iface_.CompleteRx(return_rx_list_.data(), return_rx_list_.size());
    return_rx_list_.clear();
    return_rx_parts_count_ = 0;
  }
}

void DeviceAdapter::EnqueueTx(uint32_t id, zx_status_t status) {
  auto& tx = return_tx_list_.emplace_back();
  tx.id = id;
  tx.status = status;
}

void DeviceAdapter::CommitTx() {
  if (!return_tx_list_.empty()) {
    device_iface_.CompleteTx(return_tx_list_.data(), return_tx_list_.size());
    return_tx_list_.clear();
  }
}

DeviceAdapter::DeviceAdapter(DeviceAdapterParent* parent, bool online)
    : ddk::NetworkDeviceImplProtocol<DeviceAdapter>(),
      parent_(parent),
      online_(online),
      device_info_(device_info_t{
          .tx_depth = kFifoDepth,
          .rx_depth = kFifoDepth,
          .rx_threshold = kFifoDepth / 2,
          .max_buffer_length = fuchsia_net_tun::wire::kMaxMtu,
          .buffer_alignment = 1,
          // TODO(https://fxbug.dev/75933): Lift restriction on minimum rx buffer length being the
          // MTU. That will allow us to observe rx buffer chaining on tun.
          .min_rx_buffer_length = parent->config().mtu,
          .min_tx_buffer_length = parent->config().min_tx_buffer_length,
      }),
      port_info_(port_info_t{
          .port_class = static_cast<uint8_t>(fuchsia_hardware_network::wire::DeviceClass::kUnknown),
          .rx_types_list = rx_types_.data(),
          .rx_types_count = parent->config().rx_types.size(),
          .tx_types_list = tx_types_.data(),
          .tx_types_count = parent->config().tx_types.size(),
      }) {
  // Initialize rx_types_ and tx_types_ lists from parent config.
  for (size_t i = 0; i < parent_->config().rx_types.size(); i++) {
    rx_types_[i] = static_cast<uint8_t>(parent_->config().rx_types[i]);
  }
  for (size_t i = 0; i < parent_->config().tx_types.size(); i++) {
    tx_types_[i].features = parent_->config().tx_types[i].features;
    tx_types_[i].type = static_cast<uint8_t>(parent_->config().tx_types[i].type);
    tx_types_[i].supported_flags =
        static_cast<uint32_t>(parent_->config().tx_types[i].supported_flags);
  }
}

zx::status<RxBuffer> DeviceAdapter::AllocRxSpace(size_t length) __TA_REQUIRES(rx_lock_) {
  RxBuffer buffer = vmos_.MakeEmptyRxBuffer();
  while (!rx_buffers_.empty()) {
    const rx_space_buffer_t& space = rx_buffers_.front();
    buffer.PushRxSpace(space);
    uint32_t space_length = space.region.length;
    rx_buffers_.pop();
    if (space_length >= length) {
      return zx::ok(std::move(buffer));
    }
    length -= space_length;
  }
  // Ran out of rx buffers and didn't find what we wanted, need to reclaim the space from buffer.
  ReclaimRxSpace(std::move(buffer));
  return zx::error(ZX_ERR_SHOULD_WAIT);
}

void DeviceAdapter::ReclaimRxSpace(RxBuffer buffer) __TA_REQUIRES(rx_lock_) {
  buffer.WithSpace([this](const rx_space_buffer_t& space) {
    // WithSpace is called inline.
    []() __TA_ASSERT(rx_lock_) {}();

    rx_buffers_.push(space);
  });
}

}  // namespace tun
}  // namespace network
