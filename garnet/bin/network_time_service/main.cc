// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>

#include "garnet/bin/network_time_service/service.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fit/function.h"
#include "src/lib/fsl/syslogger/init.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/syslog/cpp/logger.h"

constexpr char kServerConfigPath[] = "/pkg/data/roughtime-servers.json";

int main(int argc, char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (fsl::InitLoggerFromCommandLine(command_line, {"network_time_server"}) != ZX_OK) {
    return 1;
  }

  const std::string config_path =
      command_line.GetOptionValueWithDefault("config", kServerConfigPath);
  FX_LOGS(INFO) << "Opening client config from " << config_path;

  const std::string rtc_path =
      command_line.GetOptionValueWithDefault("rtc_path", time_server::kRealRtcDevicePath);
  FX_LOGS(INFO) << "Connecting to RTC device at " << rtc_path;
  bool immediate = command_line.HasOption("immediate");

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  network_time_service::TimeServiceImpl svc(sys::ComponentContext::Create(), config_path.c_str(),
                                            rtc_path.c_str());
  if (immediate) {
    svc.Update(3, fuchsia::timezone::TimeService::UpdateCallback([&loop](bool success) {
                 FX_LOGS(INFO) << "time sync result was " << success;
                 loop.Shutdown();
               }));
  }
  loop.Run();
  return 0;
}
