// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/tool/launch.h"

#include <fdio/limits.h>
#include <fdio/util.h>

#include <fuchsia/cpp/component.h>
#include "garnet/bin/guest/tool/serial.h"
#include "lib/app/cpp/environment_services.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/logging.h"

// Service directory channel.
static zx::channel directory;
// Application controller.
static component::ApplicationControllerPtr controller;

static component::FileDescriptorPtr CloneFileDescriptor(int fd) {
  zx_handle_t handles[FDIO_MAX_HANDLES] = {0, 0, 0};
  uint32_t types[FDIO_MAX_HANDLES] = {
      ZX_HANDLE_INVALID,
      ZX_HANDLE_INVALID,
      ZX_HANDLE_INVALID,
  };
  zx_status_t status = fdio_clone_fd(fd, 0, handles, types);
  if (status <= 0) {
    return nullptr;
  }
  component::FileDescriptorPtr result = component::FileDescriptor::New();
  result->type0 = types[0];
  result->handle0 = zx::handle(handles[0]);
  result->type1 = types[1];
  result->handle1 = zx::handle(handles[1]);
  result->type2 = types[2];
  result->handle2 = zx::handle(handles[2]);
  return result;
}

void handle_launch(int argc, const char** argv) {
  // Setup launch request.
  component::ApplicationLaunchInfo launch_info;
  launch_info.url = argv[0];
  for (int i = 0; i < argc - 1; ++i) {
    launch_info.arguments.push_back(argv[1 + i]);
  }
  launch_info.out = CloneFileDescriptor(STDOUT_FILENO);
  launch_info.err = CloneFileDescriptor(STDERR_FILENO);

  // Create service request and service directory.
  zx_status_t status =
      zx::channel::create(0, &launch_info.directory_request, &directory);
  FXL_CHECK(status == ZX_OK) << "Unable to create directory";

  // Connect to application launcher and create guest.
  component::ApplicationLauncherSyncPtr launcher;
  component::ConnectToEnvironmentService(launcher.NewRequest());
  launcher->CreateApplication(std::move(launch_info), controller.NewRequest());

  // Open the serial service of the guest and process IO.
  handle_serial([](InspectReq req) -> zx_status_t {
    return fdio_service_connect_at(directory.get(),
                                   machina::InspectService::Name_,
                                   req.TakeChannel().release());
  });
  inspect_svc.set_error_handler([] {
    std::cerr << "Launched application terminated\n";
    fsl::MessageLoop::GetCurrent()->PostQuitTask();
  });
}
