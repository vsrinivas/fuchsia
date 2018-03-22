// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/network_time/timezone.h"
#include "lib/fsl/syslogger/init.h"
#include "lib/fxl/command_line.h"

int main(int argc, char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (fsl::InitLoggerFromCommandLine(command_line, {"network_time"}) != ZX_OK) {
    return 1;
  }
  time_zone::Timezone service("/pkg/data/roughtime-servers.json");
  service.Run();
  return 0;
}
