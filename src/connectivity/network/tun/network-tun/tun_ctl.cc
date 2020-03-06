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
  std::unique_ptr<TunDevice> tun_device;
  zx_status_t status = TunDevice::Create(
      [this](TunDevice* dev) {
        async::PostTask(dispatcher_, [this, dev]() {
          devices_.erase(*dev);
          TryFireShutdownCallback();
        });
      },
      std::move(config), &tun_device);

  if (status != ZX_OK) {
    FX_LOGF(ERROR, "tun", "TunCtl: TunDevice creation failed: %s", zx_status_get_string(status));
    device.Close(status);
    return;
  }
  tun_device->Bind(std::move(device));
  devices_.push_back(std::move(tun_device));
  FX_LOG(INFO, "tun", "TunCtl: Created TunDevice");
}

void TunCtl::CreatePair(fuchsia::net::tun::DevicePairConfig config,
                        fidl::InterfaceRequest<fuchsia::net::tun::DevicePair> device_pair) {
  std::unique_ptr<TunPair> tun_pair;
  zx_status_t status = TunPair::Create(
      [this](TunPair* pair) {
        async::PostTask(dispatcher_, [this, pair]() {
          device_pairs_.erase(*pair);
          TryFireShutdownCallback();
        });
      },
      std::move(config), &tun_pair);

  if (status != ZX_OK) {
    FX_LOGF(ERROR, "tun", "TunCtl: TunPair creation failed: %s", zx_status_get_string(status));
    device_pair.Close(status);
    return;
  }
  tun_pair->Bind(std::move(device_pair));
  device_pairs_.push_back(std::move(tun_pair));
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
