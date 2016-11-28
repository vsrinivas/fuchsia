// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/tracing/src/trace_manager/app.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/log_settings.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

using namespace tracing;

namespace {

constexpr const char kDefaultConfigFile[] =
    "/system/data/trace_manager/tracing.config";

}  // namespace

int main(int argc, char** argv) {
  auto command_line = ftl::CommandLineFromArgcArgv(argc, argv);
  if (!ftl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  auto config_file =
      command_line.GetOptionValueWithDefault("config", kDefaultConfigFile);

  Config config;
  if (!config.ReadFrom(config_file)) {
    FTL_LOG(ERROR) << "Failed to read configuration from " << config_file;
    exit(1);
  }

  mtl::MessageLoop loop;
  TraceManagerApp trace_manager_app(config);
  loop.Run();
  return 0;
}
