// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"

#include "garnet/bin/network_time/logging.h"
#include "garnet/bin/network_time/time_service.h"

int main(int argc, char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line))
    return 1;
  TS_LOG(INFO) << "starting";
  if (argc != 2) {
    TS_LOG(ERROR) << "Usage: " << argv[0] << " <roughtime-servers.json>";
    return EINVAL;
  }

  timeservice::TimeService service(argv[1]);
  service.Run();
  return 0;
}
