// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/processargs.h>
#include <mxio/util.h>
#include <stdio.h>
#include <stdlib.h>

#include <string>
#include <unordered_map>
#include <vector>

#include "apps/modular/src/application_manager/root_environment_host.h"
#include "apps/modular/src/application_manager/startup_config.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/files/file.h"
#include "lib/mtl/tasks/message_loop.h"

using namespace modular;

constexpr char kDefaultConfigPath[] =
    "/system/data/application_manager/startup.config";

static void LoadStartupConfig(StartupConfig* config,
                              const std::string& config_path) {
  std::string data;
  if (!files::ReadFileToString(config_path, &data)) {
    fprintf(stderr, "application_manager: Failed to read startup config: %s\n",
            config_path.c_str());
  } else if (!config->Parse(data)) {
    fprintf(stderr, "application_manager: Failed to parse startup config: %s\n",
            config_path.c_str());
  }
}

int main(int argc, char** argv) {
  auto command_line = ftl::CommandLineFromArgcArgv(argc, argv);

  std::string config_path;
  command_line.GetOptionValue("config", &config_path);

  const auto& positional_args = command_line.positional_args();
  if (config_path.empty() && positional_args.empty())
    config_path = kDefaultConfigPath;

  std::vector<ApplicationLaunchInfoPtr> initial_apps;
  if (!config_path.empty()) {
    StartupConfig config;
    LoadStartupConfig(&config, config_path);
    initial_apps = config.TakeInitialApps();
  }

  if (!positional_args.empty()) {
    auto launch_info = ApplicationLaunchInfo::New();
    launch_info->url = positional_args[0];
    for (size_t i = 1; i < positional_args.size(); ++i)
      launch_info->arguments.push_back(positional_args[i]);
    initial_apps.push_back(std::move(launch_info));
  }

  // TODO(jeffbrown): If there's already a running instance of
  // application_manager, it might be nice to pass the request over to
  // it instead of starting a whole new instance.  Alternately, we could create
  // a separate command-line program to act as an interface for modifying
  // configuration, starting / stopping applications, listing what's running,
  // printing debugging information, etc.  Having multiple instances of
  // application manager running is not what we want, in general.

  mtl::MessageLoop message_loop;

  RootEnvironmentHost root;

  if (!initial_apps.empty()) {
    message_loop.task_runner()->PostTask([&root, &initial_apps] {
      for (auto& launch_info : initial_apps) {
        root.environment()->CreateApplication(std::move(launch_info), nullptr);
      }
    });
  }

  message_loop.Run();
  return 0;
}
