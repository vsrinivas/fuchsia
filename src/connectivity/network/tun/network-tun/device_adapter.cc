// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device_adapter.h"

#include <lib/fidl-async/cpp/bind.h>
#include <lib/sync/completion.h>
#include <lib/syslog/global.h>
#include <zircon/status.h>

namespace network {
namespace tun {

constexpr uint16_t kFifoDepth = 128;

zx_status_t DeviceAdapter::Create(async_dispatcher_t* dispatcher, DeviceAdapterParent* parent,
                                  bool online, std::unique_ptr<DeviceAdapter>* out) {
  std::unique_ptr<DeviceAdapter> adapter(new DeviceAdapter(dispatcher, parent, online));
  network_device_impl_protocol_t proto = {&adapter->network_device_impl_protocol_ops_,
                                          adapter.get()};

  zx_status_t status = NetworkDeviceInterface::Create(
      dispatcher, ddk::NetworkDeviceImplProtocolClient(&proto), "network-tun", &adapter->device_);
  if (status == ZX_OK) {
    *out = std::move(adapter);
  }
  return status;
}

zx_status_t DeviceAdapter::Bind(zx::channel req) { return device_->Bind(std::move(req)); }

zx_status_t DeviceAdapter::NetworkDeviceImplInit(const network_device_ifc_protocol_t* iface) {
  device_iface_ = ddk::NetworkDeviceIfcProtocolClient(iface);
  return ZX_OK;
}

void DeviceAdapter::NetworkDeviceImplStart(network_device_impl_start_callback callback,
                                           void* cookie) {
  {
    fbl::AutoLock lock(&state_lock_);
    has_sessions_ = true;
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
    // discard all rx buffers
    fbl::AutoLock lock(&rx_lock_);
    while (!rx_buffers_.empty()) {
      rx_buffers_.pop();
    }
  }
  {
    // discard all tx buffers
    fbl::AutoLock lock(&tx_lock_);
    while (!tx_buffers_.empty()) {
      tx_buffers_.pop();
    }
  }
  parent_->OnHasSessionsChanged(this);
  callback(cookie);
}

void DeviceAdapter::NetworkDeviceImplGetInfo(device_info_t* out_info) { *out_info = device_info_; }

void DeviceAdapter::NetworkDeviceImplGetStatus(status_t* out_status) {
  fbl::AutoLock lock(&state_lock_);
  *out_status = {
      parent_->config().mtu(),                                                              // mtu
      online_ ? static_cast<uint32_t>(fuchsia::hardware::network::StatusFlags::ONLINE) : 0  // flags
  };
}

void DeviceAdapter::NetworkDeviceImplQueueTx(const tx_buffer_t* buf_list, size_t buf_count) {
  {
    fbl::AutoLock state_lock(&state_lock_);

    if (!online_) {
      FX_VLOGF(1, "tun", "Discarding %d tx buffers because device is offline", buf_count);

      fbl::AutoLock lock(&tx_lock_);
      while (buf_count--) {
        EnqueueTx(buf_list->id, ZX_ERR_BAD_STATE);
        buf_list++;
      }
      CommitTx();
      return;
    }
    fbl::AutoLock lock(&tx_lock_);
    while (buf_count--) {
      tx_buffers_.emplace(vmos_.MakeTxBuffer(buf_list, parent_->config().report_metadata()));
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
    while (buf_count--) {
      rx_buffers_.emplace(vmos_.MakeRxSpaceBuffer(buf_list++));
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

void DeviceAdapter::SetOnline(bool online) {
  status_t new_status;
  {
    fbl::AutoLock lock(&state_lock_);
    if (online == online_) {
      return;
    }
    FX_VLOGF(1, "tun", "DeviceAdapter: SetOnline: %d", online);
    online_ = online;
    new_status.mtu = parent_->config().mtu();
    new_status.flags =
        online_ ? static_cast<uint32_t>(fuchsia::hardware::network::StatusFlags::ONLINE) : 0;

    if (!online_) {
      // if going offline, discard all pending tx buffers
      {
        fbl::AutoLock tx_lock(&tx_lock_);
        while (!tx_buffers_.empty()) {
          EnqueueTx(tx_buffers_.front().id(), ZX_ERR_BAD_STATE);
          tx_buffers_.pop();
        }
        CommitTx();
      }
    }
  }
  device_iface_.StatusChanged(&new_status);
}

bool DeviceAdapter::HasSession() {
  fbl::AutoLock lock(&state_lock_);
  return has_sessions_;
}

bool DeviceAdapter::TryGetTxBuffer(fit::callback<void(Buffer*, size_t)> callback) {
  uint32_t id;

  fbl::AutoLock lock(&tx_lock_);
  if (tx_buffers_.empty()) {
    return false;
  }
  auto& buff = tx_buffers_.front();
  auto avail = tx_buffers_.size() - 1;
  callback(&buff, avail);
  id = buff.id();
  tx_buffers_.pop();

  EnqueueTx(id, ZX_OK);
  CommitTx();
  return true;
}

zx_status_t DeviceAdapter::WriteRxFrame(fuchsia::hardware::network::FrameType frame_type,
                                        const std::vector<uint8_t>& data,
                                        const fuchsia::net::tun::FrameMetadata* meta,
                                        size_t* out_avail) {
  {
    // can't write if device is offline
    fbl::AutoLock lock(&state_lock_);
    if (!online_) {
      return ZX_ERR_BAD_STATE;
    }
  }
  uint32_t id;

  fbl::AutoLock lock(&rx_lock_);
  if (rx_buffers_.empty()) {
    return ZX_ERR_SHOULD_WAIT;
  }
  auto& buff = rx_buffers_.front();
  auto status = buff.Write(data);
  if (status != ZX_OK) {
    return status;
  }
  id = buff.id();
  rx_buffers_.pop();

  // NB: cast is only safe as long as MAX_MTU in FIDL is less than uint32 max. Guard that with a
  // static assertion.
  static_assert(fuchsia::net::tun::MAX_MTU <= std::numeric_limits<uint32_t>::max());
  EnqueueRx(frame_type, id, static_cast<uint32_t>(data.size()), meta);
  CommitRx();

  *out_avail = rx_buffers_.size();
  return ZX_OK;
}

void DeviceAdapter::CopyTo(DeviceAdapter* other, bool return_failed_buffers) {
  fbl::AutoLock tx_lock(&tx_lock_);
  fbl::AutoLock rx_lock(&other->rx_lock_);

  while (!tx_buffers_.empty()) {
    auto& tx_buff = tx_buffers_.front();
    if (other->rx_buffers_.empty()) {
      if (!return_failed_buffers) {
        // stop once we run out of rx buffers to copy to
        FX_VLOG(1, "tun", "DeviceAdapter:CopyTo: no more rx buffers");
        break;
      }
      EnqueueTx(tx_buff.id(), ZX_ERR_NO_RESOURCES);
    } else {
      auto& rx_buff = other->rx_buffers_.front();
      size_t actual;
      auto status = rx_buff.CopyFrom(&tx_buff, &actual);
      if (status != ZX_OK) {
        FX_LOGF(ERROR, "tun", "DeviceAdapter:CopyTo: Failed to copy buffer: %s",
                zx_status_get_string(status));
        EnqueueTx(tx_buff.id(), status);
      } else {
        // enqueue the data to be returned in other, and enqueue the complete tx in self.
        auto meta = tx_buff.TakeMetadata();
        if (meta) {
          meta->flags = 0;
        }
        // NB: cast is only safe as long as MAX_MTU in FIDL is less than uint32 max. Guard that with
        // a static assertion.
        static_assert(fuchsia::net::tun::MAX_MTU <= std::numeric_limits<uint32_t>::max());
        other->EnqueueRx(tx_buff.frame_type(), rx_buff.id(), static_cast<uint32_t>(actual),
                         meta.get());
        EnqueueTx(tx_buff.id(), ZX_OK);

        other->rx_buffers_.pop();
      }
    }
    tx_buffers_.pop();
  }
  CommitTx();
  other->CommitRx();
}

void DeviceAdapter::Teardown(fit::function<void()> callback) {
  device_->Teardown(std::move(callback));
}

void DeviceAdapter::TeardownSync() {
  sync_completion_t completion;
  Teardown([&completion]() { sync_completion_signal(&completion); });
  sync_completion_wait_deadline(&completion, ZX_TIME_INFINITE);
}

void DeviceAdapter::EnqueueRx(fuchsia::hardware::network::FrameType frame_type, uint32_t buffer_id,
                              uint32_t total_len, const fuchsia::net::tun::FrameMetadata* meta) {
  auto& ret = return_rx_list_.emplace_back();
  ret.id = buffer_id;
  ret.meta.frame_type = static_cast<uint8_t>(frame_type);
  ret.total_length = total_len;
  if (meta) {
    ret.meta.flags = meta->flags;
    ret.meta.info_type = static_cast<uint32_t>(meta->info_type);
    if (meta->info_type != fuchsia::hardware::network::InfoType::NO_INFO) {
      FX_LOGF(WARNING, "tun", "Unrecognized info type %d", ret.meta.info_type);
    }
  } else {
    ret.meta.flags = 0;
    ret.meta.info_type = static_cast<uint32_t>(fuchsia::hardware::network::InfoType::NO_INFO);
  }
}

void DeviceAdapter::CommitRx() {
  if (!return_rx_list_.empty()) {
    device_iface_.CompleteRx(return_rx_list_.data(), return_rx_list_.size());
    return_rx_list_.clear();
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

DeviceAdapter::DeviceAdapter(async_dispatcher_t* dispatcher, DeviceAdapterParent* parent,
                             bool online)
    : ddk::NetworkDeviceImplProtocol<DeviceAdapter>(),
      parent_(parent),
      has_sessions_(false),
      online_(online),
      device_info_(device_info_t{
          .tx_depth = kFifoDepth,
          .rx_depth = kFifoDepth,
          .device_class = static_cast<uint8_t>(fuchsia::hardware::network::DeviceClass::UNKNOWN),
          .rx_types_list = rx_types_.data(),
          .rx_types_count = parent->config().rx_types().size(),
          .tx_types_list = tx_types_.data(),
          .tx_types_count = parent->config().tx_types().size(),
          .max_buffer_length = fuchsia::net::tun::MAX_MTU,
          .buffer_alignment = 1,
          .min_rx_buffer_length = parent->config().mtu(),
      }) {
  // Initialize rx_types_ and tx_types_ lists from parent config.
  for (size_t i = 0; i < parent_->config().rx_types().size(); i++) {
    rx_types_[i] = static_cast<uint8_t>(parent_->config().rx_types()[i]);
  }
  for (size_t i = 0; i < parent_->config().tx_types().size(); i++) {
    tx_types_[i].features = parent_->config().tx_types()[i].features;
    tx_types_[i].type = static_cast<uint8_t>(parent_->config().tx_types()[i].type);
    tx_types_[i].supported_flags =
        static_cast<uint32_t>(parent_->config().tx_types()[i].supported_flags);
  }
}

}  // namespace tun
}  // namespace network
