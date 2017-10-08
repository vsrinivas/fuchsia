// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/app/cpp/environment_services.h"
#include "lib/app/fidl/application_controller.fidl.h"
#include "lib/app/fidl/application_launcher.fidl.h"

int main(int argc, const char** argv) {
  if (argc < 2) {
    fprintf(stderr, "Usage: run <program> <args>*\n");
    return 1;
  }
  auto launch_info = app::ApplicationLaunchInfo::New();
  launch_info->url = argv[1];
  for (int i = 0; i < argc - 2; ++i) {
    launch_info->arguments.push_back(argv[2 + i]);
  }

  // Connect to the ApplicationLauncher service through our static environment.
  app::ApplicationLauncherSyncPtr launcher;
  app::ConnectToEnvironmentService(GetSynchronousProxy(&launcher));

  app::ApplicationControllerSyncPtr controller;
  launcher->CreateApplication(std::move(launch_info),
                              GetSynchronousProxy(&controller));

  int32_t return_code;
  if (!controller->Wait(&return_code)) {
    fprintf(stderr, "%s exited without a return code\n", argv[1]);
    return 1;
  }
  return return_code;
}
