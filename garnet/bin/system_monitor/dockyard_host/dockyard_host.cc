// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <time.h>
#include <chrono>
#include <thread>

#include "garnet/lib/system_monitor/dockyard/dockyard.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"

int main(int argc, char** argv) {
  FXL_LOG(INFO) << "Starting dockyard host";
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line)) {
    exit(1);  // General error.
  }

  dockyard::Dockyard dockyard_app;
  std::string device;
  dockyard_app.StartCollectingFrom(device);
  while (true) {
    // In a later version of this code we will do real work here.
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
  FXL_LOG(INFO) << "Stopping dockyard host";
  return 0;
}
