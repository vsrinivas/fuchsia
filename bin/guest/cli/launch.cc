// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/cli/launch.h"

#include <fuchsia/guest/cpp/fidl.h>

#include "garnet/bin/guest/cli/serial.h"

void handle_launch(int argc, const char* argv[], async::Loop* loop,
                   component::StartupContext* context) {
  // Create environment.
  fuchsia::guest::EnvironmentManagerPtr environment_manager;
  context->ConnectToEnvironmentService(environment_manager.NewRequest());
  fuchsia::guest::EnvironmentControllerPtr environment_controller;
  environment_manager->Create(argv[0], environment_controller.NewRequest());

  // Launch guest.
  fuchsia::guest::LaunchInfo launch_info;
  launch_info.url = argv[0];
  for (int i = 0; i < argc - 1; ++i) {
    launch_info.args.push_back(argv[i + 1]);
  }
  fuchsia::guest::InstanceControllerPtr instance_controller;
  environment_controller->LaunchInstance2(
      std::move(launch_info), instance_controller.NewRequest(), [](...) {});

  // Setup serial console.
  SerialConsole console(loop);
  instance_controller->GetSerial(
      [&console](zx::socket socket) { console.Start(std::move(socket)); });

  loop->Run();
}
