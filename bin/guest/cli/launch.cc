// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/cli/launch.h"

#include <fdio/limits.h>
#include <fdio/util.h>
#include <fuchsia/cpp/component.h>
#include <fuchsia/cpp/presentation.h>
#include <fuchsia/cpp/views_v1.h>

#include "garnet/bin/guest/cli/serial.h"
#include "lib/app/cpp/environment_services.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/logging.h"
#include "lib/svc/cpp/services.h"

// Application controller.
static component::ApplicationControllerPtr g_controller;
// Guest-vended services.
static component::Services g_services;

void handle_launch(int argc, const char** argv) {
  // Setup launch request.
  component::ApplicationLaunchInfo launch_info;
  launch_info.url = argv[0];
  for (int i = 0; i < argc - 1; ++i) {
    launch_info.arguments.push_back(argv[1 + i]);
  }

  // Create service request and service directory.
  launch_info.directory_request = g_services.NewRequest();

  // Connect to application launcher and create guest.
  component::ApplicationLauncherSyncPtr launcher;
  component::ConnectToEnvironmentService(launcher.NewRequest());
  launcher->CreateApplication(std::move(launch_info),
                              g_controller.NewRequest());
  g_controller.set_error_handler([] {
    std::cerr << "Launched application terminated\n";
    fsl::MessageLoop::GetCurrent()->PostQuitTask();
  });

  // Create the framebuffer view.
  fidl::InterfacePtr<views_v1::ViewProvider> view_provider;
  g_services.ConnectToService(view_provider.NewRequest());
  fidl::InterfaceHandle<views_v1_token::ViewOwner> view_owner;
  view_provider->CreateView(view_owner.NewRequest(), nullptr);

  // Ask the presenter to display it.
  presentation::PresenterSyncPtr presenter;
  component::ConnectToEnvironmentService(presenter.NewRequest());
  presenter->Present(std::move(view_owner), nullptr);

  // Open the serial service of the guest and process IO.
  handle_serial([](InspectReq req) -> zx_status_t {
    g_services.ConnectToService(std::move(req));
    return ZX_OK;
  });
}
