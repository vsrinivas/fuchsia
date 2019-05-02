// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/network_time/timezone.h"
#include "lib/fsl/syslogger/init.h"
#include "src/lib/fxl/command_line.h"
#include "lib/syslog/cpp/logger.h"
#include "zircon/process.h"
#include "zircon/processargs.h"
#include "zircon/syscalls.h"

int main(int argc, char** argv) {
  // We need to close PA_DIRECTORY_REQUEST otherwise clients that expect us to
  // offer services won't know that we've started and are not going to offer
  // any services.
  //
  // TODO(CP-128): Explicitly doing this on long-running components should not
  // be required.
  zx_handle_close(zx_take_startup_handle(PA_DIRECTORY_REQUEST));

  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (fsl::InitLoggerFromCommandLine(command_line, {"network_time"}) != ZX_OK) {
    return 1;
  }

  const std::string config_path = command_line.GetOptionValueWithDefault(
      "config", "/pkg/data/roughtime-servers.json");
  FX_LOGS(INFO) << "Opening client config from " << config_path;

  const std::string rtc_path = command_line.GetOptionValueWithDefault(
      "rtc_path", time_server::kRealRtcDevicePath);
  FX_LOGS(INFO) << "Connecting to RTC device at " << rtc_path;

  time_server::Timezone service(config_path, rtc_path);
  bool success = service.Run();

  return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
