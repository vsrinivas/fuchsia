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
#include "garnet/bin/appmgr/dynamic_library_loader.h"
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

  fsl::MessageLoop message_loop;
  fs::ManagedVfs vfs(message_loop.async());

  app::RootEnvironmentHost root(config.TakePath(), &vfs);

  app::ApplicationControllerPtr sysmgr;
  auto run_sysmgr = [&root, &sysmgr] {
    auto launch_info = app::ApplicationLaunchInfo::New();
    launch_info->url = "sysmgr";
    root.job_holder()->CreateApplication(std::move(launch_info), sysmgr.NewRequest());
  };

  message_loop.task_runner()->PostTask([&run_sysmgr, &sysmgr] {
    run_sysmgr();
    sysmgr.set_error_handler(run_sysmgr);
  });

  message_loop.Run();
  return 0;
}
