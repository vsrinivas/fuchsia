// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include "garnet/bin/trace_manager/app.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings.h"
#include "lib/fxl/log_settings_command_line.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"

using namespace tracing;

namespace {

constexpr char kDefaultConfigFile[] = "/pkg/data/tracing.config";

}  // namespace

int main(int argc, char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  auto config_file =
      command_line.GetOptionValueWithDefault("config", kDefaultConfigFile);

  Config config;
  if (!config.ReadFrom(config_file)) {
    FXL_LOG(ERROR) << "Failed to read configuration from " << config_file;
    exit(1);
  }

  FXL_LOG(INFO) << "Trace Manager starting with config: " << config_file;

  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  TraceManagerApp trace_manager_app(config);
  loop.Run();
  return 0;
}
