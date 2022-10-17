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

zx::result<std::unique_ptr<DeviceAdapter>> DeviceAdapter::Create(async_dispatcher_t* dispatcher,
                                                                 DeviceAdapterParent* parent) {
  fbl::AllocChecker ac;
  std::unique_ptr<DeviceAdapter> adapter(new (&ac) DeviceAdapter(parent));
  if (!ac.check()) {
    return zx::error(ZX_ERR_NO_MEMORY);
  }
  network_device_impl_protocol_t proto = {
      .ops = &adapter->network_device_impl_protocol_ops_,
      .ctx = adapter.get(),
  };

  zx::result device =
      NetworkDeviceInterface::Create(dispatcher, ddk::NetworkDeviceImplProtocolClient(&proto));
  if (device.is_error()) {
    return device.take_error();
  }
  adapter->device_ = std::move(device.value());

  return zx::ok(std::move(adapter));
}

zx_status_t DeviceAdapter::Bind(fidl::ServerEnd<netdev::Device> req) {
  return device_->Bind(std::move(req));
}

zx_status_t DeviceAdapter::BindPort(uint8_t port_id, fidl::ServerEnd<netdev::Port> req) {
  return device_->BindPort(port_id, std::move(req));
}

zx_status_t DeviceAdapter::NetworkDeviceImplInit(const network_device_ifc_protocol_t* iface) {
  device_iface_ = ddk::NetworkDeviceIfcProtocolClient(iface);
  return ZX_OK;
}

void DeviceAdapter::NetworkDeviceImplStart(network_device_impl_start_callback callback,
                                           void* cookie) {
  {
    fbl::AutoLock lock(&rx_lock_);
    rx_available_ = true;
  }
  {
    fbl::AutoLock lock(&tx_lock_);
    tx_available_ = true;
  }
  callback(cookie, ZX_OK);
}

void DeviceAdapter::NetworkDeviceImplStop(network_device_impl_stop_callback callback,
                                          void* cookie) {
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
  callback(cookie);
}

void DeviceAdapter::NetworkDeviceImplGetInfo(device_info_t* out_info) { *out_info = device_info_; }

void DeviceAdapter::NetworkDeviceImplQueueTx(const tx_buffer_t* buf_list, size_t buf_count) {
  {
    fbl::AutoLock tx_lock(&tx_lock_);
    cpp20::span buffers(buf_list, buf_count);
    if (!tx_available_) {
      FX_VLOGF(1, "tun", "Discarding %d tx buffers, tx queue is invalid", tx_available_);
      for (const tx_buffer_t& b : buffers) {
        EnqueueTx(b.id, ZX_ERR_UNAVAILABLE);
      }
      CommitTx();
      return;
    }
    for (const tx_buffer_t& b : buffers) {
      if (b.meta.port >= port_online_status_.size() || !port_online_status_[b.meta.port]) {
        EnqueueTx(b.id, ZX_ERR_UNAVAILABLE);
        continue;
      }
      tx_buffers_.emplace(vmos_.MakeTxBuffer(b, parent_->config().report_metadata));
    }
    CommitTx();
  }
  parent_->OnTxAvail(this);
}

void DeviceAdapter::NetworkDeviceImplQueueRxSpace(const rx_space_buffer_t* buf_list,
                                                  size_t buf_count) {
  bool has_buffers;
  {
    fbl::AutoLock lock(&rx_lock_);
    cpp20::span buffers(buf_list, buf_count);
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

void DeviceAdapter::NetworkDeviceImplPrepareVmo(uint8_t vmo_id, zx::vmo vmo,
                                                network_device_impl_prepare_vmo_callback callback,
                                                void* cookie) {
  zx_status_t status = vmos_.RegisterVmo(vmo_id, std::move(vmo));
  callback(cookie, status);
}

void DeviceAdapter::NetworkDeviceImplReleaseVmo(uint8_t vmo_id) {
  zx_status_t status = vmos_.UnregisterVmo(vmo_id);
  if (status != ZX_OK) {
    FX_LOGF(ERROR, "tun", "DeviceAdapter failed to unregister vmo: %s",
            zx_status_get_string(status));
  }
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

void DeviceAdapter::RetainTxBuffers(fit::function<zx_status_t(TxBuffer&)> func) {
  fbl::AutoLock lock(&tx_lock_);
  for (size_t size = tx_buffers_.size(); size > 0; size--) {
    TxBuffer& buffer = tx_buffers_.front();
    zx_status_t status = func(buffer);
    if (status == ZX_OK) {
      tx_buffers_.push(std::move(buffer));
    } else {
      EnqueueTx(buffer.id(), status);
    }
    tx_buffers_.pop();
  }
  CommitTx();
}

zx::result<size_t> DeviceAdapter::WriteRxFrame(
    PortAdapter& port, fuchsia_hardware_network::wire::FrameType frame_type, const uint8_t* data,
    size_t count, const std::optional<fuchsia_net_tun::wire::FrameMetadata>& meta) {
  if (!port.online()) {
    return zx::error(ZX_ERR_BAD_STATE);
  }
  if (count > port.mtu()) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  fbl::AutoLock lock(&rx_lock_);
  if (rx_buffers_.empty()) {
    return zx::error(ZX_ERR_SHOULD_WAIT);
  }
  zx::result alloc = AllocRxSpace(count);
  if (alloc.is_error()) {
    return alloc.take_error();
  }
  RxBuffer buffer = std::move(alloc.value());
  if (zx_status_t status = buffer.Write(data, count); status != ZX_OK) {
    ReclaimRxSpace(std::move(buffer));
    return zx::error(status);
  }
  EnqueueRx(port.id(), frame_type, std::move(buffer), count, meta);
  CommitRx();

  return zx::ok(rx_buffers_.size());
}

zx::result<size_t> DeviceAdapter::WriteRxFrame(
    PortAdapter& port, fuchsia_hardware_network::wire::FrameType frame_type,
    const fidl::VectorView<uint8_t>& data,
    const std::optional<fuchsia_net_tun::wire::FrameMetadata>& meta) {
  return WriteRxFrame(port, frame_type, data.data(), data.count(), meta);
}

zx::result<size_t> DeviceAdapter::WriteRxFrame(
    PortAdapter& port, fuchsia_hardware_network::wire::FrameType frame_type,
    const std::vector<uint8_t>& data,
    const std::optional<fuchsia_net_tun::wire::FrameMetadata>& meta) {
  return WriteRxFrame(port, frame_type, data.data(), data.size(), meta);
}

void DeviceAdapter::CopyTo(DeviceAdapter* other, bool return_failed_buffers) {
  fbl::AutoLock tx_lock(&tx_lock_);
  fbl::AutoLock rx_lock(&other->rx_lock_);

  while (!tx_buffers_.empty()) {
    TxBuffer& tx_buff = tx_buffers_.front();
    zx::result alloc_rx = other->AllocRxSpace(tx_buff.length());
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
    zx::result status = rx_buff.CopyFrom(tx_buff);
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
      other->EnqueueRx(tx_buff.port_id(), tx_buff.frame_type(), std::move(rx_buff), length, meta);
      EnqueueTx(tx_buff.id(), ZX_OK);
    }
    tx_buffers_.pop();
  }
  CommitTx();
  other->CommitRx();
}

void DeviceAdapter::Teardown(fit::function<void()> callback) {
  device_->Teardown([cb = std::move(callback)]() mutable { cb(); });
}

void DeviceAdapter::TeardownSync() {
  sync_completion_t completion;
  Teardown([&completion]() { sync_completion_signal(&completion); });
  sync_completion_wait_deadline(&completion, ZX_TIME_INFINITE);
}

void DeviceAdapter::EnqueueRx(uint8_t port_id, fuchsia_hardware_network::wire::FrameType frame_type,
                              RxBuffer buffer, size_t length,
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
              .port = port_id,
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

DeviceAdapter::DeviceAdapter(DeviceAdapterParent* parent)
    : ddk::NetworkDeviceImplProtocol<DeviceAdapter>(),
      parent_(parent),
      device_info_(device_info_t{
          .tx_depth = kFifoDepth,
          .rx_depth = kFifoDepth,
          .rx_threshold = kFifoDepth / 2,
          .max_buffer_length = fuchsia_net_tun::wire::kMaxMtu,
          .buffer_alignment = 1,
          .min_rx_buffer_length = parent->config().min_rx_buffer_length,
          .min_tx_buffer_length = parent->config().min_tx_buffer_length,
      }) {
  for (std::atomic_bool& p : port_online_status_) {
    p = false;
  }
}

zx::result<RxBuffer> DeviceAdapter::AllocRxSpace(size_t length) __TA_REQUIRES(rx_lock_) {
  RxBuffer buffer = vmos_.MakeEmptyRxBuffer();
  while (!rx_buffers_.empty()) {
    const rx_space_buffer_t& space = rx_buffers_.front();
    buffer.PushRxSpace(space);
    uint64_t space_length = space.region.length;
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

void DeviceAdapter::OnPortStatusChanged(uint8_t port_id, const port_status_t& new_status) {
  port_online_status_[port_id] =
      static_cast<bool>(static_cast<netdev::wire::StatusFlags>(new_status.flags) &
                        netdev::wire::StatusFlags::kOnline);
  device_iface_.PortStatusChanged(port_id, &new_status);
}

void DeviceAdapter::AddPort(PortAdapter& port) {
  network_port_protocol_t proto = port.proto();
  device_iface_.AddPort(port.id(), proto.ctx, proto.ops);
}

void DeviceAdapter::RemovePort(uint8_t port_id) { device_iface_.RemovePort(port_id); }

}  // namespace tun
}  // namespace network
