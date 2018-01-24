// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include "lib/app/cpp/application_context.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings.h"
#include "lib/fxl/log_settings_command_line.h"

namespace {

const char kUsageString[] =
    "Options:\n"
    "  --verbose         : sets |min_log_level| to -1\n"
    "  --verbose=<level> : sets |min_log_level| to -level\n"
    "  --quiet           : sets |min_log_level| to +1 (LOG_WARNING)\n"
    "  --quiet=<level>   : sets |min_log_level| to +level\n"
    "  --log-file=<file> : sets |log_file| to file, uses default output if "
    "empty\n"
    "";

}  // namespace

int main(int argc, const char* argv[]) {
  auto cl = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(cl)) {
    std::cout << kUsageString << std::endl;
    return EXIT_FAILURE;
  }

  fsl::MessageLoop message_loop;

  // TODO(armansito): Re-enable this as part of NET-374.
  // bluetooth_service::App app(app::ApplicationContext::CreateFromStartupInfo());

  message_loop.Run();

  return EXIT_SUCCESS;
}
