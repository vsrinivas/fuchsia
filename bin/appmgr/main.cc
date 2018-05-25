// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include <string>
#include <unordered_map>
#include <vector>

#include <fs/pseudo-dir.h>
#include <fs/service.h>
#include <fs/synchronous-vfs.h>
#include <fs/vfs.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

#include "garnet/bin/appmgr/dynamic_library_loader.h"
#include "garnet/bin/appmgr/realm.h"
#include "garnet/bin/appmgr/root_loader.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/log_settings.h"

namespace {

constexpr char kRootLabel[] = "app";

void PublishRootDir(component::Realm* root, fs::SynchronousVfs* vfs) {
  static zx_handle_t request = zx_get_startup_handle(PA_DIRECTORY_REQUEST);
  if (request == ZX_HANDLE_INVALID)
    return;
  fbl::RefPtr<fs::PseudoDir> dir(fbl::AdoptRef(new fs::PseudoDir()));
  auto svc = fbl::AdoptRef(new fs::Service([root](zx::channel channel) {
    return root->BindSvc(std::move(channel));
  }));
  dir->AddEntry("hub", root->hub_dir());
  dir->AddEntry("svc", svc);

  vfs->ServeDirectory(dir, zx::channel(request));
  request = ZX_HANDLE_INVALID;
}

}  // namespace

int main(int argc, char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);

  async::Loop loop(&kAsyncLoopConfigMakeDefault);

  fs::SynchronousVfs vfs(loop.async());
  component::RootLoader root_loader;
  fbl::RefPtr<fs::PseudoDir> directory(fbl::AdoptRef(new fs::PseudoDir()));
  directory->AddEntry(
      component::Loader::Name_,
      fbl::AdoptRef(new fs::Service([&root_loader](zx::channel channel) {
        root_loader.AddBinding(
            fidl::InterfaceRequest<component::Loader>(std::move(channel)));
        return ZX_OK;
      })));

  zx::channel h1, h2;
  if (zx::channel::create(0, &h1, &h2) < 0)
    return -1;
  if (vfs.ServeDirectory(directory, std::move(h2)) != ZX_OK)
    return -1;
  component::Realm root_realm(nullptr, std::move(h1), kRootLabel);
  fs::SynchronousVfs publish_vfs(loop.async());
  PublishRootDir(&root_realm, &publish_vfs);

  component::ComponentControllerPtr sysmgr;
  auto run_sysmgr = [&root_realm, &sysmgr] {
    component::LaunchInfo launch_info;
    launch_info.url = "sysmgr";
    root_realm.CreateApplication(std::move(launch_info), sysmgr.NewRequest());
  };

  async::PostTask(loop.async(), [&run_sysmgr, &sysmgr] {
    run_sysmgr();
    sysmgr.set_error_handler(run_sysmgr);
  });

  loop.Run();
  return 0;
}
