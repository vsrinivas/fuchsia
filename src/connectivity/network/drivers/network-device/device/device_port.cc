// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device_port.h"

#include <lib/async/cpp/task.h>

#include "log.h"

namespace network::internal {

DevicePort::DevicePort(async_dispatcher_t* dispatcher, uint8_t id,
                       ddk::NetworkPortProtocolClient port,
                       std::unique_ptr<MacAddrDeviceInterface> mac, TeardownCallback on_teardown)
    : dispatcher_(dispatcher),
      port_id_(id),
      port_(port),
      mac_(std::move(mac)),
      on_teardown_(std::move(on_teardown)) {
  port.GetInfo(&info_);
  ZX_ASSERT(info_.rx_types_count <= netdev::wire::kMaxFrameTypes);
  ZX_ASSERT(info_.tx_types_count <= netdev::wire::kMaxFrameTypes);

  // Copy frame type support from port info since lists in banjo are always unowned.
  std::copy_n(info_.rx_types_list, info_.rx_types_count, supported_rx_.begin());
  info_.rx_types_list = supported_rx_.data();
  std::copy_n(info_.tx_types_list, info_.tx_types_count, supported_tx_.begin());
  info_.tx_types_list = supported_tx_.data();
}

void DevicePort::StatusChanged(const port_status_t& new_status) {
  fbl::AutoLock lock(&lock_);
  for (auto& w : watchers_) {
    w.PushStatus(new_status);
  }
}

void DevicePort::BindStatusWatcher(fidl::ServerEnd<netdev::StatusWatcher> watcher,
                                   uint32_t buffer) {
  fbl::AutoLock lock(&lock_);
  if (teardown_started_) {
    // Don't install new watchers after teardown has started.
    return;
  }

  fbl::AllocChecker ac;
  auto n_watcher = fbl::make_unique_checked<StatusWatcher>(&ac, buffer);
  if (!ac.check()) {
    return;
  }

  zx_status_t status =
      n_watcher->Bind(dispatcher_, std::move(watcher), [this](StatusWatcher* watcher) {
        fbl::AutoLock lock(&lock_);
        watchers_.erase(*watcher);
        MaybeFinishTeardown();
      });

  if (status != ZX_OK) {
    LOGF_ERROR("network-device: Failed to bind watcher: %s", zx_status_get_string(status));
    return;
  }

  port_status_t device_status;
  port_.GetStatus(&device_status);
  n_watcher->PushStatus(device_status);
  watchers_.push_back(std::move(n_watcher));
}

bool DevicePort::MaybeFinishTeardown() {
  if (teardown_started_ && on_teardown_ && watchers_.is_empty() && !mac_ && on_teardown_) {
    // Always finish teardown on dispatcher to evade deadlock opportunity on DeviceInterface ports
    // lock.
    async::PostTask(dispatcher_, [this, call = std::move(on_teardown_)]() mutable { call(*this); });
    return true;
  }
  return false;
}

void DevicePort::Teardown() {
  fbl::AutoLock lock(&lock_);
  if (teardown_started_) {
    return;
  }
  teardown_started_ = true;
  // Attempt to conclude the teardown immediately if we have no live resources.
  if (MaybeFinishTeardown()) {
    return;
  }
  for (auto& watcher : watchers_) {
    watcher.Unbind();
  }
  if (mac_) {
    mac_->Teardown([this]() {
      // Always dispatch mac teardown callback to our dispatcher.
      async::PostTask(dispatcher_, [this]() {
        fbl::AutoLock lock(&lock_);
        // Dispose of mac entirely on teardown complete.
        mac_ = nullptr;
        MaybeFinishTeardown();
      });
    });
  }
}

void DevicePort::BindMac(fidl::ServerEnd<netdev::MacAddressing> req) {
  fbl::AutoLock lock(&lock_);
  if (teardown_started_) {
    return;
  }
  if (!mac_) {
    req.Close(ZX_ERR_NOT_SUPPORTED);
    return;
  }
  zx_status_t status = mac_->Bind(dispatcher_, std::move(req));
  if (status != ZX_OK) {
    LOGF_ERROR("network-device: failed to bind to MacAddr on port %d: %s", port_id_,
               zx_status_get_string(status));
  }
}

void DevicePort::SessionAttached() {
  size_t session_count;
  {
    fbl::AutoLock lock(&lock_);
    session_count = ++attached_sessions_count_;
  }
  NotifySessionCount(session_count);
}

void DevicePort::SessionDetached() {
  size_t session_count;
  {
    fbl::AutoLock lock(&lock_);
    ZX_ASSERT_MSG(attached_sessions_count_ > 0, "detached the same port twice");
    session_count = --attached_sessions_count_;
  }
  NotifySessionCount(session_count);
}

void DevicePort::NotifySessionCount(size_t new_count) {
  // Port active changes whenever the new count on session attaching or detaching edges away from
  // zero.
  if (new_count <= 1) {
    port_.SetActive(new_count != 0);
  }
}

bool DevicePort::IsValidRxFrameType(uint8_t frame_type) const {
  fbl::Span rx_types(info_.rx_types_list, info_.rx_types_count);
  return std::any_of(rx_types.begin(), rx_types.end(),
                     [frame_type](const uint8_t& t) { return t == frame_type; });
}

bool DevicePort::IsValidTxFrameType(uint8_t frame_type) const {
  fbl::Span tx_types(info_.tx_types_list, info_.tx_types_count);
  return std::any_of(tx_types.begin(), tx_types.end(),
                     [frame_type](const tx_support_t& t) { return t.type == frame_type; });
}

}  // namespace network::internal
