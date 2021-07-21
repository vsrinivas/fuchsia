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

void TunCtl::CreateDevice(CreateDeviceRequestView request, CreateDeviceCompleter::Sync& completer) {
  // Create a DeviceConfig and DevicePortConfig from legacy configuration.
  fidl::FidlAllocator alloc;
  fuchsia_net_tun::wire::DeviceConfig2 fidl_device_config(alloc);
  fidl_device_config.set_base(alloc, fuchsia_net_tun::wire::BaseDeviceConfig(alloc));
  fuchsia_net_tun::wire::DevicePortConfig fidl_port_config(alloc);
  fidl_port_config.set_base(alloc, fuchsia_net_tun::wire::BasePortConfig(alloc));

  fuchsia_net_tun::wire::DeviceConfig& request_config = request->config;
  if (!request_config.has_base()) {
    request->device.Close(ZX_ERR_INVALID_ARGS);
    return;
  }
  fuchsia_net_tun::wire::BaseConfig& base = request_config.base();
  if (base.has_mtu()) {
    fidl_port_config.base().set_mtu(fidl::ObjectView<uint32_t>::FromExternal(&base.mtu()));
  }
  if (base.has_rx_types()) {
    fidl_port_config.base().set_rx_types(
        fidl::ObjectView<fidl::VectorView<fuchsia_hardware_network::wire::FrameType>>::FromExternal(
            &base.rx_types()));
  }
  if (base.has_tx_types()) {
    fidl_port_config.base().set_tx_types(
        fidl::ObjectView<fidl::VectorView<fuchsia_hardware_network::wire::FrameTypeSupport>>::
            FromExternal(&base.tx_types()));
  }
  if (base.has_report_metadata()) {
    fidl_device_config.base().set_report_metadata(
        fidl::ObjectView<bool>::FromExternal(&base.report_metadata()));
  }
  if (base.has_min_tx_buffer_length()) {
    fidl_device_config.base().set_min_tx_buffer_length(
        fidl::ObjectView<uint32_t>::FromExternal(&base.min_tx_buffer_length()));
  }
  if (request_config.has_online()) {
    fidl_port_config.set_online(fidl::ObjectView<bool>::FromExternal(&request_config.online()));
  }
  if (request_config.has_blocking()) {
    fidl_device_config.set_blocking(
        fidl::ObjectView<bool>::FromExternal(&request_config.blocking()));
  }
  if (request_config.has_mac()) {
    fidl_port_config.set_mac(
        fidl::ObjectView<fuchsia_net::wire::MacAddress>::FromExternal(&request_config.mac()));
  }

  fidl_port_config.base().set_id(alloc, TunDevice::kLegacyDefaultPort);

  zx::status tun_device = TunDevice::Create(
      [this](TunDevice* dev) {
        async::PostTask(dispatcher_, [this, dev]() {
          devices_.erase(*dev);
          TryFireShutdownCallback();
        });
      },
      fidl_device_config);

  if (tun_device.is_error()) {
    FX_LOGF(ERROR, "tun", "TunCtl: legacy TunDevice creation failed: %s",
            tun_device.status_string());
    request->device.Close(tun_device.error_value());
    return;
  }

  fidl::ServerEnd<fuchsia_net_tun::Port> empty_request;
  if (zx_status_t status = tun_device->AddPort(fidl_port_config, empty_request); status != ZX_OK) {
    FX_LOGF(ERROR, "tun", "TunCtl: legacy TunDevice add port failed: %s",
            zx_status_get_string(status));
  }

  auto& value = tun_device.value();
  value->BindLegacy(std::move(request->device));
  devices_.push_back(std::move(value));
  FX_LOG(INFO, "tun", "TunCtl: Created TunDevice");
}

void TunCtl::CreateDevice2(CreateDevice2RequestView request,
                           CreateDevice2Completer::Sync& completer) {
  zx::status tun_device = TunDevice::Create(
      [this](TunDevice* dev) {
        async::PostTask(dispatcher_, [this, dev]() {
          devices_.erase(*dev);
          TryFireShutdownCallback();
        });
      },
      request->config);

  if (tun_device.is_error()) {
    FX_LOGF(ERROR, "tun", "TunCtl: TunDevice creation failed: %s", tun_device.status_string());
    request->device.Close(tun_device.error_value());
    return;
  }
  auto& value = tun_device.value();
  value->Bind(std::move(request->device));
  devices_.push_back(std::move(value));
  FX_LOG(INFO, "tun", "TunCtl: Created TunDevice");
}

void TunCtl::CreatePair(CreatePairRequestView request, CreatePairCompleter::Sync& completer) {
  zx::status tun_pair = TunPair::Create(
      [this](TunPair* pair) {
        async::PostTask(dispatcher_, [this, pair]() {
          device_pairs_.erase(*pair);
          TryFireShutdownCallback();
        });
      },
      std::move(request->config));

  if (tun_pair.is_error()) {
    FX_LOGF(ERROR, "tun", "TunCtl: TunPair creation failed: %s", tun_pair.status_string());
    request->device_pair.Close(tun_pair.status_value());
    return;
  }
  auto& value = tun_pair.value();
  value->Bind(std::move(request->device_pair));
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
