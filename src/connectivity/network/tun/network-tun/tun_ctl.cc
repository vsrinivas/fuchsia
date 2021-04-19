// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tun_ctl.h"

#include <lib/async/cpp/task.h>
#include <lib/syslog/global.h>
#include <zircon/status.h>

#include "tun_device.h"

namespace network {
namespace tun {

void TunCtl::CreateDevice(fuchsia::net::tun::DeviceConfig config,
                          fidl::InterfaceRequest<fuchsia::net::tun::Device> device) {
  zx::status tun_device = TunDevice::Create(
      [this](TunDevice* dev) {
        async::PostTask(dispatcher_, [this, dev]() {
          devices_.erase(*dev);
          TryFireShutdownCallback();
        });
      },
      std::move(config));

  if (tun_device.is_error()) {
    FX_LOGF(ERROR, "tun", "TunCtl: TunDevice creation failed: %s", tun_device.status_string());
    device.Close(tun_device.error_value());
    return;
  }
  auto& value = tun_device.value();
  value->Bind(std::move(device));
  devices_.push_back(std::move(value));
  FX_LOG(INFO, "tun", "TunCtl: Created TunDevice");
}

void TunCtl::CreatePair(fuchsia::net::tun::DevicePairConfig config,
                        fidl::InterfaceRequest<fuchsia::net::tun::DevicePair> device_pair) {
  zx::status tun_pair = TunPair::Create(
      [this](TunPair* pair) {
        async::PostTask(dispatcher_, [this, pair]() {
          device_pairs_.erase(*pair);
          TryFireShutdownCallback();
        });
      },
      std::move(config));

  if (tun_pair.is_error()) {
    FX_LOGF(ERROR, "tun", "TunCtl: TunPair creation failed: %s", tun_pair.status_string());
    device_pair.Close(tun_pair.status_value());
    return;
  }
  auto& value = tun_pair.value();
  value->Bind(std::move(device_pair));
  device_pairs_.push_back(std::move(value));
  FX_LOG(INFO, "tun", "TunCtl: Created TunPair");
}

void TunCtl::SetSafeShutdownCallback(fit::callback<void()> shutdown_callback) {
  async::PostTask(dispatcher_, [this, callback = std::move(shutdown_callback)]() mutable {
    ZX_ASSERT_MSG(!shutdown_callback_, "Shutdown callback already installed");
    shutdown_callback_ = std::move(callback);
    TryFireShutdownCallback();
  });
}

void TunCtl::TryFireShutdownCallback() {
  if (shutdown_callback_ && device_pairs_.is_empty() && devices_.is_empty()) {
    shutdown_callback_();
  }
}

}  // namespace tun
}  // namespace network
