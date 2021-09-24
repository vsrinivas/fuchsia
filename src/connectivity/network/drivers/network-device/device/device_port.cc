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
  port_info_t info;
  port.GetInfo(&info);
  ZX_ASSERT_MSG(info.rx_types_count <= netdev::wire::kMaxFrameTypes,
                "too many port rx types: %ld > %d", info.rx_types_count,
                netdev::wire::kMaxFrameTypes);
  ZX_ASSERT_MSG(info.tx_types_count <= netdev::wire::kMaxFrameTypes,
                "too many port tx types: %ld > %d", info.tx_types_count,
                netdev::wire::kMaxFrameTypes);

  port_class_ = static_cast<netdev::wire::DeviceClass>(info.port_class);

  supported_rx_count_ = 0;
  for (const uint8_t& rx_support : fbl::Span(info.rx_types_list, info.rx_types_count)) {
    supported_rx_[supported_rx_count_++] = static_cast<netdev::wire::FrameType>(rx_support);
  }
  supported_tx_count_ = 0;
  for (const tx_support_t& tx_support : fbl::Span(info.tx_types_list, info.tx_types_count)) {
    supported_tx_[supported_tx_count_++] = {
        .type = static_cast<netdev::wire::FrameType>(tx_support.type),
        .features = tx_support.features,
        .supported_flags = static_cast<netdev::wire::TxFlags>(tx_support.supported_flags)};
  }
}

void DevicePort::StatusChanged(const port_status_t& new_status) {
  fbl::AutoLock lock(&lock_);
  for (auto& w : watchers_) {
    w.PushStatus(new_status);
  }
}

void DevicePort::GetStatusWatcher(GetStatusWatcherRequestView request,
                                  GetStatusWatcherCompleter::Sync& _completer) {
  fbl::AutoLock lock(&lock_);
  if (teardown_started_) {
    // Don't install new watchers after teardown has started.
    return;
  }

  fbl::AllocChecker ac;
  auto n_watcher = fbl::make_unique_checked<StatusWatcher>(&ac, request->buffer);
  if (!ac.check()) {
    return;
  }

  zx_status_t status =
      n_watcher->Bind(dispatcher_, std::move(request->watcher), [this](StatusWatcher* watcher) {
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
  if (teardown_started_ && on_teardown_ && watchers_.is_empty() && !mac_ && on_teardown_ &&
      bindings_.is_empty()) {
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
  for (auto& binding : bindings_) {
    binding.Unbind();
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

void DevicePort::GetMac(GetMacRequestView request, GetMacCompleter::Sync& _completer) {
  fidl::ServerEnd req = std::move(request->mac);

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
  fbl::AutoLock lock(&lock_);
  NotifySessionCount(++attached_sessions_count_);
}

void DevicePort::SessionDetached() {
  fbl::AutoLock lock(&lock_);
  ZX_ASSERT_MSG(attached_sessions_count_ > 0, "detached the same port twice");
  NotifySessionCount(--attached_sessions_count_);
}

void DevicePort::NotifySessionCount(size_t new_count) {
  if (teardown_started_) {
    // Skip all notifications if tearing down.
    return;
  }
  // Port active changes whenever the new count on session attaching or detaching edges away from
  // zero.
  if (new_count <= 1) {
    // Always post notifications for later on dispatcher so the port implementation can safely call
    // back into the core device with no risk of deadlocks.
    async::PostTask(dispatcher_, [this, active = new_count != 0]() { port_.SetActive(active); });
  }
}

bool DevicePort::IsValidRxFrameType(netdev::wire::FrameType frame_type) const {
  fbl::Span rx_types(supported_rx_.begin(), supported_rx_count_);
  return std::any_of(rx_types.begin(), rx_types.end(),
                     [frame_type](const netdev::wire::FrameType& t) { return t == frame_type; });
}

bool DevicePort::IsValidTxFrameType(netdev::wire::FrameType frame_type) const {
  fbl::Span tx_types(supported_tx_.begin(), supported_tx_count_);
  return std::any_of(
      tx_types.begin(), tx_types.end(),
      [frame_type](const netdev::wire::FrameTypeSupport& t) { return t.type == frame_type; });
}

void DevicePort::Bind(fidl::ServerEnd<netdev::Port> req) {
  fbl::AllocChecker ac;
  std::unique_ptr<Binding> binding(new (&ac) Binding);
  if (!ac.check()) {
    req.Close(ZX_ERR_NO_MEMORY);
    return;
  }

  fbl::AutoLock lock(&lock_);
  // Capture a pointer to the binding so we can erase it in the unbound function.
  Binding* binding_ptr = binding.get();
  binding->Bind(fidl::BindServer(dispatcher_, std::move(req), this,
                                 [binding_ptr](DevicePort* port, fidl::UnbindInfo /*unused*/,
                                               fidl::ServerEnd<netdev::Port> /*unused*/) {
                                   // Always complete unbind later to avoid deadlock in case bind
                                   // fails synchronously.
                                   async::PostTask(port->dispatcher_, [port, binding_ptr]() {
                                     fbl::AutoLock lock(&port->lock_);
                                     port->bindings_.erase(*binding_ptr);
                                     port->MaybeFinishTeardown();
                                   });
                                 }));

  bindings_.push_front(std::move(binding));
}

void DevicePort::GetInfo(GetInfoRequestView request, GetInfoCompleter::Sync& completer) {
  netdev::wire::PortInfo::Frame_ frame;
  netdev::wire::PortInfo port_info(
      fidl::ObjectView<netdev::wire::PortInfo::Frame_>::FromExternal(&frame));
  // NB: Need to copy port identifier out because ObjectView wants a non const pointer.
  uint8_t port_id = port_id_;
  auto tx_support = fidl::VectorView<netdev::wire::FrameTypeSupport>::FromExternal(
      supported_tx_.data(), supported_tx_count_);
  auto rx_support = fidl::VectorView<netdev::wire::FrameType>::FromExternal(supported_rx_.data(),
                                                                            supported_rx_count_);
  port_info.set_id(port_id)
      .set_class_(port_class_)
      .set_tx_types(fidl::ObjectView<decltype(tx_support)>::FromExternal(&tx_support))
      .set_rx_types(fidl::ObjectView<decltype(rx_support)>::FromExternal(&rx_support));

  completer.Reply(std::move(port_info));
}

void DevicePort::GetStatus(GetStatusRequestView request, GetStatusCompleter::Sync& completer) {
  port_status_t status;
  port_.GetStatus(&status);
  WithWireStatus(
      [&completer](netdev::wire::PortStatus wire_status) { completer.Reply(wire_status); }, status);
}

}  // namespace network::internal
