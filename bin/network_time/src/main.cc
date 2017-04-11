// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ftl/command_line.h"
#include "lib/ftl/log_settings_command_line.h"

#include "logging.h"
#include "time_service.h"

int main(int argc, char** argv) {
  auto command_line = ftl::CommandLineFromArgcArgv(argc, argv);
  if (!ftl::SetLogSettingsFromCommandLine(command_line))
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
