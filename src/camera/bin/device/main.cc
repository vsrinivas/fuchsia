// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/wait.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/logger.h>

#include <string>

#include "src/camera/bin/device/device_impl.h"

int main(int argc, char* argv[]) {
  syslog::InitLogger({"camera", "camera_device"});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::Create();

  // Verify arguments.
  if (argc != 2 || argv[1][0] == '\0') {
    FX_PLOGS(FATAL, ZX_ERR_INVALID_ARGS)
        << "Component must be initialized with outgoing service name.";
    return EXIT_FAILURE;
  }
  std::string outgoing_service_name = argv[1];

  // Connect to required environment services.
  fidl::InterfaceHandle<fuchsia::camera2::hal::Controller> controller;
  zx_status_t status = context->svc()->Connect(controller.NewRequest());
  if (status != ZX_OK) {
    FX_PLOGS(FATAL, status) << "Failed to request controller service.";
    return EXIT_FAILURE;
  }

  // Create the device and publish its service.
  auto result = DeviceImpl::Create(std::move(controller));
  if (result.is_error()) {
    FX_PLOGS(FATAL, result.error()) << "Failed to create device.";
    return EXIT_FAILURE;
  }
  auto device = result.take_value();

  // TODO(44628): publish discoverable service name once supported
  status = context->outgoing()->AddPublicService(device->GetHandler(), outgoing_service_name);
  if (status != ZX_OK) {
    FX_PLOGS(FATAL, status) << "Failed to publish service.";
    return EXIT_FAILURE;
  }

  // Post a quit task in the event the device enters a bad state.
  auto event = device->GetBadStateEvent();
  async::Wait wait(event.get(), ZX_EVENT_SIGNALED, 0,
                   [&](async_dispatcher_t* dispatcher, async::Wait* wait, zx_status_t status,
                       const zx_packet_signal_t* signal) {
                     FX_LOGS(FATAL) << "Device signaled bad state.";
                     loop.Quit();
                   });
  ZX_ASSERT(wait.Begin(loop.dispatcher()) == ZX_OK);

  // Run should return only if the device enters an unrecoverable state.
  status = loop.Run();
  FX_PLOGS(FATAL, status) << "Loop exited unexpectedly.";
  return EXIT_FAILURE;
}
