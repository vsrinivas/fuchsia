// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include <string>
#include <unordered_map>
#include <vector>

#include <fs/vfs.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

#include "garnet/bin/appmgr/config.h"
#include "garnet/bin/appmgr/root_environment_host.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/log_settings.h"

constexpr char kDefaultConfigPath[] = "/system/data/appmgr/initial.config";

int main(int argc, char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);

  std::string config_file;
  command_line.GetOptionValue("config", &config_file);

  const auto& positional_args = command_line.positional_args();
  if (config_file.empty() && positional_args.empty())
    config_file = kDefaultConfigPath;

  app::Config config;
  if (!config_file.empty()) {
    config.ReadIfExistsFrom(config_file);
  }

  auto initial_apps = config.TakeInitialApps();
  if (!positional_args.empty()) {
    auto launch_info = app::ApplicationLaunchInfo::New();
    launch_info->url = positional_args[0];
    for (size_t i = 1; i < positional_args.size(); ++i)
      launch_info->arguments.push_back(positional_args[i]);
    initial_apps.push_back(std::move(launch_info));
  }

  fsl::MessageLoop message_loop;
  fs::ManagedVfs vfs(message_loop.async());

  app::RootEnvironmentHost root(config.TakePath(), &vfs);

  if (!initial_apps.empty()) {
    message_loop.task_runner()->PostTask([&root, &initial_apps] {
      for (auto& launch_info : initial_apps) {
        root.job_holder()->CreateApplication(std::move(launch_info), nullptr);
      }
    });
  }

  message_loop.Run();
  return 0;
}
