// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include <string>
#include <unordered_map>
#include <vector>

#include <fs/managed-vfs.h>
#include <fs/pseudo-dir.h>
#include <fs/service.h>
#include <fs/vfs.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

#include "garnet/bin/appmgr/config.h"
#include "garnet/bin/appmgr/dynamic_library_loader.h"
#include "garnet/bin/appmgr/root_application_loader.h"
#include "garnet/bin/appmgr/job_holder.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/log_settings.h"

constexpr char kDefaultConfigPath[] = "/system/data/appmgr/initial.config";
constexpr char kRootLabel[] = "root";

int main(int argc, char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);

  std::string config_file;
  command_line.GetOptionValue("config", &config_file);

  const auto& positional_args = command_line.positional_args();
  if (config_file.empty() && positional_args.empty())
    config_file = kDefaultConfigPath;

  component::Config config;
  if (!config_file.empty()) {
    config.ReadIfExistsFrom(config_file);
  }

  fsl::MessageLoop message_loop;

  fs::ManagedVfs vfs(message_loop.async());
  component::RootApplicationLoader root_loader(config.TakePath());
  fbl::RefPtr<fs::PseudoDir> directory(fbl::AdoptRef(new fs::PseudoDir()));
  directory->AddEntry(
      component::ApplicationLoader::Name_,
      fbl::AdoptRef(new fs::Service([&root_loader](zx::channel channel) {
        root_loader.AddBinding(
            fidl::InterfaceRequest<component::ApplicationLoader>(
                std::move(channel)));
        return ZX_OK;
      })));

  zx::channel h1, h2;
  if (zx::channel::create(0, &h1, &h2) < 0)
    return -1;
  if (vfs.ServeDirectory(directory, std::move(h2)) != ZX_OK)
    return -1;
  component::JobHolder root_job_holder(nullptr, std::move(h1), kRootLabel);

  component::ApplicationControllerPtr sysmgr;
  auto run_sysmgr = [&root_job_holder, &sysmgr] {
    component::ApplicationLaunchInfo launch_info;
    launch_info.url = "sysmgr";
    root_job_holder.CreateApplication(
        std::move(launch_info), sysmgr.NewRequest());
  };

  message_loop.task_runner()->PostTask([&run_sysmgr, &sysmgr] {
    run_sysmgr();
    sysmgr.set_error_handler(run_sysmgr);
  });

  message_loop.Run();
  return 0;
}
