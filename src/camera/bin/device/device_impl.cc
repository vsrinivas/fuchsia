// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/bin/device/device_impl.h"

#include <lib/async-loop/default.h>
#include <lib/syslog/cpp/logger.h>
#include <lib/zx/time.h>
#include <zircon/types.h>

// Waits until |deadline| for |event| to signal all bits in |signals_all| or any bits in
// |signals_any|, accumulating signaled bits into |pending|.
static zx_status_t WaitMixed(const zx::event& event, zx_signals_t signals_all,
                             zx_signals_t signals_any, zx::time deadline, zx_signals_t* pending) {
  ZX_ASSERT(pending);
  *pending = ZX_SIGNAL_NONE;
  zx_status_t status = ZX_OK;
  while (status == ZX_OK) {
    zx_signals_t signaled{};
    status = event.wait_one(signals_all | signals_any, deadline, &signaled);
    *pending |= signaled;
    if ((*pending & signals_all) == signals_all) {
      return status;
    }
    if ((*pending & signals_any) != ZX_SIGNAL_NONE) {
      return status;
    }
  }
  return status;
}

DeviceImpl::DeviceImpl() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

DeviceImpl::~DeviceImpl() { loop_.Shutdown(); }

fit::result<std::unique_ptr<DeviceImpl>, zx_status_t> DeviceImpl::Create(
    fidl::InterfaceHandle<fuchsia::camera2::hal::Controller> controller) {
  auto device = std::make_unique<DeviceImpl>();

  constexpr auto kControllerDisconnected = ZX_USER_SIGNAL_0;
  constexpr auto kGetDeviceInfoReturned = ZX_USER_SIGNAL_1;
  constexpr auto kGetConfigsReturned = ZX_USER_SIGNAL_2;
  zx::event event;
  zx_status_t status = zx::event::create(0, &event);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status);
    return fit::error(status);
  }

  // Bind the controller interface and get some initial startup information.

  status = device->controller_.Bind(std::move(controller), device->loop_.dispatcher());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status);
    return fit::error(status);
  }

  zx_status_t controller_status = ZX_OK;
  device->controller_.set_error_handler([&](zx_status_t status) {
    FX_PLOGS(ERROR, status) << "Controller server disconnected during initialization.";
    controller_status = status;
    ZX_ASSERT(event.signal(0, kControllerDisconnected) == ZX_OK);
  });

  device->controller_->GetDeviceInfo(
      [&, device = device.get()](fuchsia::camera2::DeviceInfo device_info) {
        device->device_info_ = std::move(device_info);
        ZX_ASSERT(event.signal(0, kGetDeviceInfoReturned) == ZX_OK);
      });

  zx_status_t get_configs_status = ZX_OK;
  device->controller_->GetConfigs(
      [&, device = device.get()](fidl::VectorPtr<fuchsia::camera2::hal::Config> configs,
                                 zx_status_t status) {
        get_configs_status = status;
        if (configs.has_value()) {
          device->configs_ = std::move(configs.value());
        }
        ZX_ASSERT(event.signal(0, kGetConfigsReturned) == ZX_OK);
      });

  // Start the device thread and begin processing messages.

  status = device->loop_.StartThread("Camera Device Thread");
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status);
    return fit::error(status);
  }

  // Wait for either an error, or for all expected callbacks to occur.

  zx_signals_t signaled{};
  status = WaitMixed(event, kGetDeviceInfoReturned | kGetConfigsReturned, kControllerDisconnected,
                     zx::time::infinite(), &signaled);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status);
    return fit::error(status);
  }
  if (signaled & kControllerDisconnected) {
    FX_PLOGS(ERROR, controller_status);
    return fit::error(status);
  }

  // Rebind the controller error handler.

  device->controller_.set_error_handler(
      fit::bind_member(device.get(), &DeviceImpl::OnControllerDisconnected));

  return fit::ok(std::move(device));
}

void DeviceImpl::OnControllerDisconnected(zx_status_t status) { FX_PLOGS(ERROR, status); }

DeviceImpl::Client::Client() : binding_(this) {}

fit::result<std::unique_ptr<DeviceImpl::Client>, zx_status_t> DeviceImpl::Client::Create() {
  return fit::error(ZX_ERR_NOT_SUPPORTED);
}
