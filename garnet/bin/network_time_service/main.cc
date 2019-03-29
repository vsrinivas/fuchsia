// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>

#include "garnet/bin/network_time_service/service.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fsl/syslogger/init.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/logging.h"

namespace network_time_service {

constexpr char kServerConfigPath[] = "/pkg/data/roughtime-servers.json";

class MainService {
 public:
  MainService()
      : time_service_(component::StartupContext::CreateFromStartupInfo(),
                      kServerConfigPath) {}

 private:
  TimeServiceImpl time_service_;
};

}  // namespace network_time_service

int main(int argc, char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (fsl::InitLoggerFromCommandLine(command_line, {"network_time_server"}) !=
      ZX_OK) {
    return 1;
  }

  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  network_time_service::MainService svc;
  loop.Run();
  return 0;
}
