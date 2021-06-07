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

zx::status<std::unique_ptr<TunPair>> TunPair::Create(
    fit::callback<void(TunPair*)> teardown, fuchsia_net_tun::wire::DevicePairConfig config) {
  std::optional validated_config = DevicePairConfig::Create(config);
  if (!validated_config.has_value()) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  fbl::AllocChecker ac;
  std::unique_ptr<TunPair> tun(
      new (&ac) TunPair(std::move(teardown), std::move(validated_config.value())));
  if (!ac.check()) {
    return zx::error(ZX_ERR_NO_MEMORY);
  }

  zx::status left =
      DeviceAdapter::Create(tun->loop_.dispatcher(), tun.get(), false, tun->config_.mac_left);
  if (left.is_error()) {
    FX_LOGF(ERROR, "tun", "TunDevice::Init device init left failed with %s", left.status_string());
    return left.take_error();
  }
  tun->left_ = std::move(left.value());

  zx::status right =
      DeviceAdapter::Create(tun->loop_.dispatcher(), tun.get(), false, tun->config_.mac_right);
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

void TunPair::ConnectProtocols(ConnectProtocolsRequestView request,
                               ConnectProtocolsCompleter::Sync& completer) {
  if (request->requests.has_left()) {
    ConnectProtocols(*left_, std::move(request->requests.left()));
  }
  if (request->requests.has_right()) {
    ConnectProtocols(*right_, std::move(request->requests.right()));
  }
}

void TunPair::ConnectProtocols(DeviceAdapter& device, fuchsia_net_tun::wire::Protocols protos) {
  if (protos.has_network_device()) {
    device.Bind(std::move(protos.network_device()));
  }
  if (device.mac() && protos.has_mac_addressing()) {
    device.mac()->Bind(loop_.dispatcher(), std::move(protos.mac_addressing()));
  }
}

void TunPair::OnHasSessionsChanged(DeviceAdapter* device) {
  fbl::AutoLock lock(&power_lock_);
  bool online = left_->HasSession() && right_->HasSession();
  left_->SetOnline(online);
  right_->SetOnline(online);
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

void TunPair::OnMacStateChanged(MacAdapter* adapter) {
  // Do nothing, TunPair doesn't care about mac state.
}

}  // namespace tun
}  // namespace network
