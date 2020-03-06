// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tun_pair.h"

#include <lib/async-loop/default.h>
#include <lib/sync/completion.h>
#include <lib/syslog/global.h>
#include <zircon/status.h>

#include "util.h"

namespace network {
namespace tun {

TunPair::TunPair(fit::callback<void(TunPair*)> teardown, fuchsia::net::tun::DevicePairConfig config)
    : teardown_callback_(std::move(teardown)),
      config_(std::move(config)),
      loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
      binding_(this) {
  binding_.set_error_handler([this](zx_status_t) { Teardown(); });
}

zx_status_t TunPair::Create(fit::callback<void(TunPair*)> teardown,
                            fuchsia::net::tun::DevicePairConfig config,
                            std::unique_ptr<TunPair>* out) {
  if (!TryConsolidateDevicePairConfig(&config)) {
    return ZX_ERR_INVALID_ARGS;
  }
  std::unique_ptr<TunPair> tun(new TunPair(std::move(teardown), std::move(config)));

  zx_status_t status =
      DeviceAdapter::Create(tun->loop_.dispatcher(), tun.get(), false, &tun->left_);
  if (status != ZX_OK) {
    FX_LOGF(ERROR, "tun", "TunDevice::Init device init left failed with %s",
            zx_status_get_string(status));
    return status;
  }

  status = DeviceAdapter::Create(tun->loop_.dispatcher(), tun.get(), false, &tun->right_);
  if (status != ZX_OK) {
    FX_LOGF(ERROR, "tun", "TunDevice::Init device init right failed with %s",
            zx_status_get_string(status));
    return status;
  }

  if (tun->config_.has_mac_left()) {
    status = MacAdapter::Create(tun.get(), tun->config_.mac_left(), true, &tun->mac_left_);
    if (status != ZX_OK) {
      FX_LOGF(ERROR, "tun", "TunDevice::Init mac init left failed with %s",
              zx_status_get_string(status));
      return status;
    }
  }
  if (tun->config_.has_mac_right()) {
    status = MacAdapter::Create(tun.get(), tun->config_.mac_right(), true, &tun->mac_right_);
    if (status != ZX_OK) {
      FX_LOGF(ERROR, "tun", "TunDevice::Init mac init right failed with %s",
              zx_status_get_string(status));
      return status;
    }
  }

  thrd_t thread;
  status = tun->loop_.StartThread("tun-device-pair", &thread);
  if (status != ZX_OK) {
    return status;
  }
  tun->loop_thread_ = thread;

  *out = std::move(tun);
  return ZX_OK;
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
  sync_completion_t left_mac_teardown;
  sync_completion_t right_mac_teardown;
  AdapterTeardown(left_, &left_device_teardown);
  AdapterTeardown(right_, &right_device_teardown);
  AdapterTeardown(mac_left_, &left_mac_teardown);
  AdapterTeardown(mac_right_, &right_mac_teardown);
  sync_completion_wait(&left_device_teardown, ZX_TIME_INFINITE);
  sync_completion_wait(&right_device_teardown, ZX_TIME_INFINITE);
  sync_completion_wait(&left_mac_teardown, ZX_TIME_INFINITE);
  sync_completion_wait(&right_mac_teardown, ZX_TIME_INFINITE);
  loop_.Shutdown();
  FX_VLOG(1, "tun", "TunDevice destroyed");
}

void TunPair::Teardown() {
  if (teardown_callback_) {
    teardown_callback_(this);
  }
}

void TunPair::Bind(fidl::InterfaceRequest<fuchsia::net::tun::DevicePair> req) {
  binding_.Bind(std::move(req), loop_.dispatcher());
}

void TunPair::ConnectProtocols(fuchsia::net::tun::DevicePairEnds requests) {
  if (requests.has_left()) {
    ConnectProtocols(left_, mac_left_, std::move(*requests.mutable_left()));
  }
  if (requests.has_right()) {
    ConnectProtocols(right_, mac_right_, std::move(*requests.mutable_right()));
  }
}

void TunPair::ConnectProtocols(const std::unique_ptr<DeviceAdapter>& device,
                               const std::unique_ptr<MacAdapter>& mac,
                               fuchsia::net::tun::Protocols protos) {
  if (device && protos.has_network_device()) {
    device->Bind(protos.mutable_network_device()->TakeChannel());
  }
  if (mac && protos.has_mac_addressing()) {
    mac->Bind(loop_.dispatcher(), protos.mutable_mac_addressing()->TakeChannel());
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
    fallible = config_.fallible_transmit_left();
  } else {
    target = left_.get();
    fallible = config_.fallible_transmit_right();
  }
  device->CopyTo(target, fallible);
}

void TunPair::OnRxAvail(DeviceAdapter* device) {
  DeviceAdapter* source;
  bool fallible;
  // Device is the receiver, fallible is read for the source end.
  if (device == left_.get()) {
    source = right_.get();
    fallible = config_.fallible_transmit_right();
  } else {
    source = left_.get();
    fallible = config_.fallible_transmit_left();
  }
  source->CopyTo(device, fallible);
}

void TunPair::OnMacStateChanged(MacAdapter* adapter) {
  // Do nothing, TunPair doesn't care about mac state.
}

}  // namespace tun
}  // namespace network
