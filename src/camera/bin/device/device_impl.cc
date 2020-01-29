// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/bin/device/device_impl.h"

#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/syslog/cpp/logger.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include "lib/async/default.h"
#include "src/camera/bin/device/messages.h"
#include "src/camera/bin/device/util.h"

DeviceImpl::DeviceImpl() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

DeviceImpl::~DeviceImpl() {
  Unbind(controller_);
  loop_.Quit();
  loop_.JoinThreads();
}

fit::result<std::unique_ptr<DeviceImpl>, zx_status_t> DeviceImpl::Create(
    fidl::InterfaceHandle<fuchsia::camera2::hal::Controller> controller) {
  auto device = std::make_unique<DeviceImpl>();

  ZX_ASSERT(zx::event::create(0, &device->bad_state_event_) == ZX_OK);

  constexpr auto kControllerDisconnected = ZX_USER_SIGNAL_0;
  constexpr auto kGetDeviceInfoReturned = ZX_USER_SIGNAL_1;
  constexpr auto kGetConfigsReturned = ZX_USER_SIGNAL_2;
  zx::event event;
  ZX_ASSERT(zx::event::create(0, &event) == ZX_OK);

  // Bind the controller interface and get some initial startup information.

  ZX_ASSERT(device->controller_.Bind(std::move(controller), device->loop_.dispatcher()) == ZX_OK);

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
        if (status == ZX_OK) {
          if (configs.has_value()) {
            device->configs_ = std::move(configs.value());
            for (const auto& config : device->configs_) {
              auto result = Convert(config);
              if (result.is_error()) {
                get_configs_status = result.error();
                FX_PLOGS(ERROR, get_configs_status);
                break;
              }
              device->configurations_.push_back(result.take_value());
            }
          } else {
            get_configs_status = ZX_ERR_INTERNAL;
            FX_PLOGS(ERROR, get_configs_status) << "Controller returned null configs list.";
          }
        }
        ZX_ASSERT(event.signal(0, kGetConfigsReturned) == ZX_OK);
      });

  // Start the device thread and begin processing messages.

  ZX_ASSERT(device->loop_.StartThread("Camera Device Thread") == ZX_OK);

  // Wait for either an error, or for all expected callbacks to occur.

  zx_signals_t signaled{};
  ZX_ASSERT(WaitMixed(event, kGetDeviceInfoReturned | kGetConfigsReturned, kControllerDisconnected,
                      zx::time::infinite(), &signaled) == ZX_OK);
  if (signaled & kControllerDisconnected) {
    FX_PLOGS(ERROR, controller_status);
    return fit::error(controller_status);
  }

  // Rebind the controller error handler.

  ZX_ASSERT(async::PostTask(device->loop_.dispatcher(), [device = device.get()]() {
              device->controller_.set_error_handler(
                  fit::bind_member(device, &DeviceImpl::OnControllerDisconnected));
            }) == ZX_OK);

  return fit::ok(std::move(device));
}

fidl::InterfaceRequestHandler<fuchsia::camera3::Device> DeviceImpl::GetHandler() {
  return fit::bind_member(this, &DeviceImpl::OnNewRequest);
}

zx::event DeviceImpl::GetBadStateEvent() {
  zx::event event;
  ZX_ASSERT(bad_state_event_.duplicate(ZX_RIGHTS_BASIC, &event) == ZX_OK);
  return event;
}

void DeviceImpl::OnNewRequest(fidl::InterfaceRequest<fuchsia::camera3::Device> request) {
  auto task = [this, request = std::move(request)]() mutable {
    if (!clients_.empty()) {
      FX_PLOGS(INFO, ZX_ERR_ALREADY_BOUND) << Messages::kDeviceAlreadyBound;
      request.Close(ZX_ERR_ALREADY_BOUND);
      return;
    }
    auto client = std::make_unique<Client>(*this, client_id_next_, std::move(request));
    clients_.emplace(client_id_next_++, std::move(client));
  };
  ZX_ASSERT(async::PostTask(loop_.dispatcher(), std::move(task)) == ZX_OK);
}

void DeviceImpl::OnControllerDisconnected(zx_status_t status) {
  FX_PLOGS(ERROR, status) << "Controller disconnected unexpectedly.";
  ZX_ASSERT(bad_state_event_.signal(0, ZX_EVENT_SIGNALED) == ZX_OK);
}

void DeviceImpl::PostRemoveClient(uint64_t id) {
  ZX_ASSERT(async::PostTask(loop_.dispatcher(), [this, id]() { clients_.erase(id); }) == ZX_OK);
}

void DeviceImpl::PostSetConfiguration(uint32_t index) {
  ZX_ASSERT(async::PostTask(loop_.dispatcher(), [this, index]() { SetConfiguration(index); }) ==
            ZX_OK);
}

void DeviceImpl::SetConfiguration(uint32_t index) {
  streams_.clear();
  for (uint32_t stream_index = 0; stream_index < configurations_[index].streams.size();
       ++stream_index) {
    streams_.emplace_back(nullptr);
  }
  current_configuration_index_ = index;
}
