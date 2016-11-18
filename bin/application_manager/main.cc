// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/processargs.h>
#include <mxio/util.h>
#include <stdlib.h>

#include <string>
#include <unordered_map>
#include <vector>

#include "apps/modular/src/application_manager/application_loader.h"
#include "apps/modular/src/application_manager/command_listener.h"
#include "apps/modular/src/application_manager/config.h"
#include "apps/modular/src/application_manager/root_environment_host.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/files/file.h"
#include "lib/ftl/log_settings.h"
#include "lib/mtl/tasks/message_loop.h"

using namespace modular;

constexpr char kDefaultConfigPath[] =
    "/system/data/application_manager/applications.config";

int main(int argc, char** argv) {
  auto command_line = ftl::CommandLineFromArgcArgv(argc, argv);

  std::string config_file;
  command_line.GetOptionValue("config", &config_file);

  const auto& positional_args = command_line.positional_args();
  if (config_file.empty() && positional_args.empty())
    config_file = kDefaultConfigPath;

  Config config;
  if (!config_file.empty()) {
    config.ReadIfExistsFrom(config_file);
  }

  auto initial_apps = config.TakeInitialApps();
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

  ApplicationLoader loader(config.TakePath());
  RootEnvironmentHost root(&loader);

  if (!initial_apps.empty()) {
    message_loop.task_runner()->PostTask([&root, &initial_apps] {
      for (auto& launch_info : initial_apps) {
        root.environment()->CreateApplication(std::move(launch_info), nullptr);
      }
    });
  }

  std::unique_ptr<CommandListener> command_listener;
  mx::channel command_channel(
      mxio_get_startup_handle(MX_HND_TYPE_APPLICATION_LAUNCHER));
  if (command_channel) {
    command_listener.reset(
        new CommandListener(root.environment(), std::move(command_channel)));
  }

  message_loop.Run();
  return 0;
}
