// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/bin/usb_device_watcher/device_instance.h"

#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <lib/sys/service/cpp/service.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>

#include "fuchsia/io/cpp/fidl.h"
#include "lib/fit/function.h"
#include "lib/sys/cpp/service_directory.h"

namespace camera {

fpromise::result<std::unique_ptr<DeviceInstance>, zx_status_t> DeviceInstance::Create(
    fidl::InterfaceHandle<fuchsia::hardware::camera::Device> camera,
    async_dispatcher_t* dispatcher) {
  auto instance = std::make_unique<DeviceInstance>();
  instance->dispatcher_ = dispatcher;

  // Bind the camera channel.
  zx_status_t status = instance->camera_.Bind(std::move(camera), instance->dispatcher_);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status);
    return fpromise::error(status);
  }
  instance->camera_.set_error_handler([instance = instance.get()](zx_status_t status) {
    FX_PLOGS(WARNING, status) << "Camera device server disconnected.";
    instance->camera_ = nullptr;
  });
  return fpromise::ok(std::move(instance));
}

}  // namespace camera
