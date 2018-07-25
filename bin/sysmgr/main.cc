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
    char buf[PATH_MAX];
    if (strlcpy(buf, kConfigDir, PATH_MAX) >= PATH_MAX) {
      FXL_LOG(FATAL) << "Config directory path too long";
    } else {
      const size_t dir_len = strlen(buf);
      DIR* cfg_dir = opendir(kConfigDir);
      if (cfg_dir != nullptr) {
        for (dirent* cfg = readdir(cfg_dir); cfg != nullptr;
             cfg = readdir(cfg_dir)) {
          if (strcmp(".", cfg->d_name) == 0 || strcmp("..", cfg->d_name) == 0) {
            continue;
          }
          if (strlcat(buf, cfg->d_name, PATH_MAX) >= PATH_MAX) {
            FXL_LOG(WARNING) << "Could not read config file, path too long";
            continue;
          }
          if (!config.ParseFromFile(buf)) {
            break;
          }
          buf[dir_len] = '\0';
        }
        closedir(cfg_dir);
      } else {
        FXL_LOG(WARNING) << "Could not open config directory " << kConfigDir;
      }
    }
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
