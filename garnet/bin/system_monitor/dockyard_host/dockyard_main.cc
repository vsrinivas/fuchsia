// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <chrono>
#include <thread>

#include "garnet/bin/system_monitor/dockyard_host/dockyard_host.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"

int main(int argc, char** argv) {
  FXL_LOG(INFO) << "Starting dockyard host";
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line)) {
    exit(1);  // General error.
  }

  DockyardHost host;
  host.StartCollectingFrom("");
  while (true) {
    // In a later version of this code we will do real work here.
    std::this_thread::sleep_for(std::chrono::seconds(1));
    host.Dockyard().ProcessRequests();
  }
  FXL_LOG(INFO) << "Stopping dockyard host";
  exit(0);
}
