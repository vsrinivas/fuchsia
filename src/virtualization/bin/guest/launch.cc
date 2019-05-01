// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/guest/launch.h"

#include <fuchsia/guest/cpp/fidl.h>
#include <src/lib/fxl/strings/string_printf.h>

#include "src/virtualization/bin/guest/serial.h"

void handle_launch(int argc, const char* argv[], async::Loop* loop,
                   sys::ComponentContext* context) {
  // Create environment.
  fuchsia::guest::EnvironmentManagerPtr environment_manager;
  context->svc()->Connect(environment_manager.NewRequest());
  fuchsia::guest::EnvironmentControllerPtr environment_controller;
  environment_manager->Create(argv[0], environment_controller.NewRequest());

  // Launch guest.
  fuchsia::guest::LaunchInfo launch_info;
  launch_info.url = fxl::StringPrintf(
      "fuchsia-pkg://fuchsia.com/%s#meta/%s.cmx", argv[0], argv[0]);
  for (int i = 0; i < argc - 1; ++i) {
    launch_info.args.push_back(argv[i + 1]);
  }
  fuchsia::guest::InstanceControllerPtr instance_controller;
  environment_controller->LaunchInstance(
      std::move(launch_info), instance_controller.NewRequest(), [](...) {});

  // Setup serial console.
  SerialConsole console(loop);
  instance_controller->GetSerial(
      [&console](zx::socket socket) { console.Start(std::move(socket)); });

  loop->Run();
}
