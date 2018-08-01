// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <string.h>
#include <sys/types.h>
#include <string>

#include <lib/async-loop/cpp/loop.h>
#include "garnet/bin/sysmgr/app.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"

constexpr char kConfigDir[] = "/system/data/sysmgr/";

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line))
    return 1;
  
  sysmgr::Config config;
  if (command_line.HasOption("config")) {
    std::string config_data;
    command_line.GetOptionValue("config", &config_data);
    config.ParseFromString(config_data, "command line");
  } else {
    config.ParseFromDirectory(kConfigDir);
  }

  if (config.HasError()) {
    FXL_LOG(ERROR) << "Parsing config failed:\n" << config.error_str();
    return ZX_ERR_INVALID_ARGS;
  }

  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  sysmgr::App app(std::move(config));

  loop.Run();
  return 0;
}
