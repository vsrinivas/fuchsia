// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tun_pair.h"

#include <lib/async-loop/default.h>
#include <lib/sync/completion.h>
#include <lib/syslog/global.h>

#include <fbl/auto_lock.h>

namespace network {
namespace tun {

TunPair::TunPair(fit::callback<void(TunPair*)> teardown, DevicePairConfig config)
    : teardown_callback_(std::move(teardown)),
      config_(std::move(config)),
      loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

zx::result<std::unique_ptr<TunPair>> TunPair::Create(
    fit::callback<void(TunPair*)> teardown, fuchsia_net_tun::wire::DevicePairConfig config) {
  fbl::AllocChecker ac;
  std::unique_ptr<TunPair> tun(new (&ac) TunPair(std::move(teardown), DevicePairConfig(config)));
  if (!ac.check()) {
    return zx::error(ZX_ERR_NO_MEMORY);
  }

  zx::result left = DeviceAdapter::Create(tun->loop_.dispatcher(), tun.get());
  if (left.is_error()) {
    FX_LOGF(ERROR, "tun", "TunDevice::Init device init left failed with %s", left.status_string());
    return left.take_error();
  }
  tun->left_ = std::move(left.value());

  zx::result right = DeviceAdapter::Create(tun->loop_.dispatcher(), tun.get());
  if (right.is_error()) {
    FX_LOGF(ERROR, "tun", "TunDevice::Init device init right failed with %s",
            right.status_string());
    return right.take_error();
  }
  tun->right_ = std::move(right.value());

  thrd_t thread;
  if (zx_status_t status = tun->loop_.StartThread("tun-device-pair", &thread); status != ZX_OK) {
    return zx::error(status);
  }
  tun->loop_thread_ = thread;

  return zx::ok(std::move(tun));
}

// Helper function to perform synchronous teardown of all adapters in a TunPair.
// Can be called with std::unique_ptr to MacAdapter or DeviceAdapter.
// Used in TunPair destructor.
template <typename T>
inline void AdapterTeardown(const T& adapter, sync_completion_t* completion) {
  if (adapter) {
    adapter->Teardown([completion] { sync_completion_signal(completion); });
  } else {
    sync_completion_signal(completion);
  }
}

TunPair::~TunPair() {
  if (loop_thread_.has_value()) {
    // not allowed to destroy a tun pair on the loop thread, will cause deadlock
    ZX_ASSERT(loop_thread_.value() != thrd_current());
  }
  // make sure that both devices and mac adapters are torn down:
  sync_completion_t left_device_teardown;
  sync_completion_t right_device_teardown;
  AdapterTeardown(left_, &left_device_teardown);
  AdapterTeardown(right_, &right_device_teardown);
  sync_completion_wait(&left_device_teardown, ZX_TIME_INFINITE);
  sync_completion_wait(&right_device_teardown, ZX_TIME_INFINITE);
  loop_.Shutdown();
  FX_VLOG(1, "tun", "TunDevice destroyed");
}

void TunPair::Teardown() {
  if (teardown_callback_) {
    teardown_callback_(this);
  }
}

void TunPair::Bind(fidl::ServerEnd<fuchsia_net_tun::DevicePair> req) {
  binding_ =
      fidl::BindServer(loop_.dispatcher(), std::move(req), this,
                       [](TunPair* impl, fidl::UnbindInfo,
                          fidl::ServerEnd<fuchsia_net_tun::DevicePair>) { impl->Teardown(); });
}

void TunPair::AddPort(AddPortRequestView request, AddPortCompleter::Sync& completer) {
  zx_status_t status = [&request, this]() {
    std::optional maybe_config = DevicePairPortConfig::Create(request->config);
    if (!maybe_config.has_value()) {
      return ZX_ERR_INVALID_ARGS;
    }
    DevicePairPortConfig& config = *maybe_config;
    fbl::AutoLock lock(&power_lock_);
    Ports& ports = ports_[config.port_id];
    if (ports.left || ports.right) {
      return ZX_ERR_ALREADY_EXISTS;
    }

    zx::result left = Port::Create(this, true, config, std::move(config.mac_left));
    if (left.is_error()) {
      return left.status_value();
    }
    zx::result right = Port::Create(this, false, config, std::move(config.mac_right));
    if (right.is_error()) {
      return right.status_value();
    }

    ports.left = std::move(*left);
    ports.right = std::move(*right);

    left_->AddPort(ports.left->adapter());
    right_->AddPort(ports.right->adapter());
    return ZX_OK;
  }();

  if (status == ZX_OK) {
    completer.ReplySuccess();
  } else {
    completer.ReplyError(status);
  }
}

void TunPair::RemovePort(RemovePortRequestView request, RemovePortCompleter::Sync& completer) {
  zx_status_t status = [port_id = request->id, this]() {
    fbl::AutoLock lock(&power_lock_);
    if (port_id >= ports_.size() || !ports_[port_id].left || !ports_[port_id].right) {
      return ZX_ERR_NOT_FOUND;
    }
    left_->RemovePort(port_id);
    right_->RemovePort(port_id);
    return ZX_OK;
  }();
  if (status == ZX_OK) {
    completer.ReplySuccess();
  } else {
    completer.ReplyError(status);
  }
}

void TunPair::GetLeft(GetLeftRequestView request, GetLeftCompleter::Sync& _completer) {
  zx_status_t status = left_->Bind(std::move(request->device));
  if (status != ZX_OK) {
    FX_LOGF(ERROR, "tun", "bind to left device failed: %s", zx_status_get_string(status));
  }
}

void TunPair::GetRight(GetRightRequestView request, GetRightCompleter::Sync& _completer) {
  zx_status_t status = right_->Bind(std::move(request->device));
  if (status != ZX_OK) {
    FX_LOGF(ERROR, "tun", "bind to right device failed: %s", zx_status_get_string(status));
  }
}

void TunPair::GetLeftPort(GetLeftPortRequestView request, GetLeftPortCompleter::Sync& _completer) {
  zx_status_t status = left_->BindPort(request->id, std::move(request->port));
  if (status != ZX_OK) {
    FX_LOGF(ERROR, "tun", "bind to left port %d failed: %s", request->id,
            zx_status_get_string(status));
  }
}
void TunPair::GetRightPort(GetRightPortRequestView request,
                           GetRightPortCompleter::Sync& _completer) {
  zx_status_t status = right_->BindPort(request->id, std::move(request->port));
  if (status != ZX_OK) {
    FX_LOGF(ERROR, "tun", "bind to right port %d failed: %s", request->id,
            zx_status_get_string(status));
  }
}

void TunPair::OnTxAvail(DeviceAdapter* device) {
  DeviceAdapter* target;
  bool fallible;
  // Device is the transmitter, fallible is read for the end that matches device.
  if (device == left_.get()) {
    target = right_.get();
    fallible = config_.fallible_transmit_left;
  } else {
    target = left_.get();
    fallible = config_.fallible_transmit_right;
  }
  device->CopyTo(target, fallible);
}

void TunPair::OnRxAvail(DeviceAdapter* device) {
  DeviceAdapter* source;
  bool fallible;
  // Device is the receiver, fallible is read for the source end.
  if (device == left_.get()) {
    source = right_.get();
    fallible = config_.fallible_transmit_right;
  } else {
    source = left_.get();
    fallible = config_.fallible_transmit_left;
  }
  source->CopyTo(device, fallible);
}

zx::result<std::unique_ptr<TunPair::Port>> TunPair::Port::Create(
    TunPair* parent, bool left, const BasePortConfig& config,
    std::optional<fuchsia_net::wire::MacAddress> mac) {
  std::unique_ptr<Port> port(new Port(parent, left));
  std::unique_ptr<MacAdapter> mac_adapter;
  if (mac.has_value()) {
    zx::result status = MacAdapter::Create(port.get(), std::move(*mac), true);
    if (status.is_error()) {
      return status.take_error();
    }
    mac_adapter = std::move(*status);
  }
  port->adapter_ = std::make_unique<PortAdapter>(port.get(), config, std::move(mac_adapter));
  return zx::ok(std::move(port));
}

void TunPair::Port::OnHasSessionsChanged(PortAdapter& port) {
  TunPair& parent = *parent_;
  fbl::AutoLock lock(&parent.power_lock_);
  uint8_t port_id = port.id();
  Ports& ports = parent.ports_[port_id];
  // Race between teardown of left or right ports might be observed. If any of the ends is not set
  // assume port teardown is in process and skip this update.
  if (!ports.left || !ports.right) {
    return;
  }
  PortAdapter& left_adapter = ports.left->adapter();
  PortAdapter& right_adapter = ports.right->adapter();
  bool online = left_adapter.has_sessions() && right_adapter.has_sessions();
  left_adapter.SetOnline(online);
  right_adapter.SetOnline(online);
}

void TunPair::Port::OnMacStateChanged(MacAdapter* adapter) {
  // Do nothing, TunPair doesn't care about mac state.
}

void TunPair::Port::OnPortStatusChanged(PortAdapter& port, const port_status_t& new_status) {
  TunPair& parent = *parent_;
  DeviceAdapter& device = [l = left_, &parent]() -> DeviceAdapter& {
    if (l) {
      return *parent.left_;
    } else {
      return *parent.right_;
    }
  }();
  device.OnPortStatusChanged(port.id(), new_status);
}

void TunPair::Port::OnPortDestroyed(PortAdapter& port) {
  TunPair& parent = *parent_;
  fbl::AutoLock lock(&parent.power_lock_);
  Ports& ports = parent.ports_[port.id()];
  if (left_) {
    ports.left = nullptr;
  } else {
    ports.right = nullptr;
  }
}

}  // namespace tun
}  // namespace network
