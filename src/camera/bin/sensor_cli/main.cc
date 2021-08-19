// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>

#include "src/camera/bin/sensor_cli/debug_client.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"

constexpr std::string_view kOptionMode = "mode";
constexpr std::string_view kOptionModeValueDefault = "0";

bool ParseUIntValue(char* str, uint32_t* value) {
  char* ptr = nullptr;
  *value = static_cast<uint32_t>(strtoul(str, &ptr, 0));
  ZX_ASSERT(ptr != nullptr);
  return (*ptr == '\0');
}

int main(int argc, char* argv[]) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  // Parse command line options.
  // Currently, only --mode=<value> is supported.
  const auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  std::string mode_value =
      command_line.GetOptionValueWithDefault(kOptionMode, kOptionModeValueDefault);

  uint32_t mode;
  char* str = strdup(mode_value.c_str());
  if (!ParseUIntValue(str, &mode)) {
    FX_LOGS(ERROR) << "Invalid sensor mode: \"" << mode_value << "\"";
    return EXIT_FAILURE;
  }

  // Start the client to send the options.
  camera::DebugClient client(loop);
  client.Start(mode);
  loop.Run();
  return EXIT_SUCCESS;
}
