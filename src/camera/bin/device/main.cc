// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/logger.h>

#include "src/camera/bin/device/device_impl.h"

int main(int argc, char* argv[]) {
  syslog::InitLogger({"camera", "camera_device"});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::Create();

  fidl::InterfaceHandle<fuchsia::camera2::hal::Controller> controller;
  zx_status_t status = context->svc()->Connect(controller.NewRequest());
  if (status != ZX_OK) {
    FX_PLOGS(FATAL, status) << "Failed to request controller service.";
    return EXIT_FAILURE;
  }

  auto result = DeviceImpl::Create(std::move(controller));
  if (result.is_error()) {
    FX_PLOGS(FATAL, status) << "Failed to create device.";
    return EXIT_FAILURE;
  }

  // Run should never return.
  status = loop.Run();
  FX_PLOGS(FATAL, status) << "Loop exited unexpectedly.";
  return EXIT_FAILURE;
}
