// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <string.h>
#include <sys/types.h>

#include <string>

#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"
#include "src/sys/sysmgr/app.h"

constexpr char kConfigDataDir[] = "/config/data/";

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line)) {
    return 1;
  }

  sysmgr::Config config;
  config.ParseFromDirectory(kConfigDataDir);
  if (config.HasError()) {
    FX_LOGS(ERROR) << "Parsing config failed:\n" << config.error_str();
    return ZX_ERR_INVALID_ARGS;
  }

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto component_context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  FX_DCHECK(component_context);
  sysmgr::App app(std::move(config), component_context->svc(), &loop);
  loop.Run();
  return 0;
}
