// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/network_time/time_service.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/syslogger_init.h"

int main(int argc, char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (fxl::InitLoggerFromCommandLine(command_line, {"network_time"}) != ZX_OK) {
    return 1;
  }
  timeservice::TimeService service("/pkg/data/roughtime-servers.json");
  service.Run();
  return 0;
}
