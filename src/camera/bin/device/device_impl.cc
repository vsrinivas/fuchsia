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

DeviceImpl::~DeviceImpl() {
  Unbind(controller_);
  loop_.Quit();
  loop_.JoinThreads();
}

fidl::InterfaceRequestHandler<fuchsia::camera3::Device> DeviceImpl::GetHandler() {
  return fit::bind_member(this, &DeviceImpl::OnNewRequest);
}

void DeviceImpl::OnNewRequest(fidl::InterfaceRequest<fuchsia::camera3::Device> request) {
  if (!clients_.empty()) {
    FX_PLOGS(INFO, ZX_ERR_ALREADY_BOUND) << Messages::kDeviceAlreadyBound;
    request.Close(ZX_ERR_ALREADY_BOUND);
  }

  auto result = Client::Create(*this, client_id_next_, std::move(request));
  if (result.is_error()) {
    FX_PLOGS(ERROR, result.error());
    return;
  }

  clients_[client_id_next_] = result.take_value();

  ++client_id_next_;
}

static fit::result<fuchsia::camera3::Configuration, zx_status_t> Convert(
    const fuchsia::camera2::hal::Config& config) {
  if (config.stream_configs.empty()) {
    FX_PLOGS(ERROR, ZX_ERR_INTERNAL) << "Config reported no streams.";
    return fit::error(ZX_ERR_INTERNAL);
  }
  fuchsia::camera3::Configuration ret{};
  for (const auto& stream_config : config.stream_configs) {
    if (stream_config.image_formats.empty()) {
      FX_PLOGS(ERROR, ZX_ERR_INTERNAL) << "Stream reported no image formats.";
      return fit::error(ZX_ERR_INTERNAL);
    }
    fuchsia::camera3::StreamProperties stream_properties{};
    stream_properties.frame_rate.numerator = stream_config.frame_rate.frames_per_sec_numerator;
    stream_properties.frame_rate.denominator = stream_config.frame_rate.frames_per_sec_denominator;
    stream_properties.image_format = stream_config.image_formats[0];
    for (const auto& format : stream_config.image_formats) {
      fuchsia::camera3::Resolution resolution{
          .coded_size{
              .width = static_cast<int32_t>(format.coded_width),
              .height = static_cast<int32_t>(format.coded_height),
          },
          .bytes_per_row = format.bytes_per_row,
      };
      stream_properties.supported_resolutions.push_back(resolution);
    }
    ret.streams.push_back(std::move(stream_properties));
  }
  return fit::ok(std::move(ret));
}

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

  status = async::PostTask(device->loop_.dispatcher(), [device = device.get()]() {
    device->controller_.set_error_handler(
        fit::bind_member(device, &DeviceImpl::OnControllerDisconnected));
  });
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status);
    return fit::error(status);
  }

  return fit::ok(std::move(device));
}

void DeviceImpl::OnControllerDisconnected(zx_status_t status) {
  FX_PLOGS(ERROR, status) << "Controller disconnected unexpectedly.";
  clients_.clear();
}

void DeviceImpl::PostRemoveClient(uint64_t id) {
  zx_status_t status = async::PostTask(loop_.dispatcher(), [=]() {
    auto it = clients_.find(id);
    if (it != clients_.end()) {
      clients_.erase(it);
    }
  });
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status);
  }
}

void DeviceImpl::PostSetConfiguration(uint32_t index) {
  zx_status_t status = async::PostTask(loop_.dispatcher(), [=]() { SetConfiguration(index); });
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status);
  }
}

void DeviceImpl::SetConfiguration(uint32_t index) {
  std::vector<std::unique_ptr<StreamImpl>> streams;

  for (uint32_t stream_index = 0; stream_index < configurations_[index].streams.size();
       ++stream_index) {
    auto result = StreamImpl::Create(nullptr);
    if (result.is_error()) {
      FX_PLOGS(ERROR, result.error())
          << "Failed to connect to controller config " << index << " stream " << stream_index;
      clients_.clear();
      return;
    }
    streams.push_back(result.take_value());
  }

  streams_ = std::move(streams);
  current_configuration_index_ = index;
}
