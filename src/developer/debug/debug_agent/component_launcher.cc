// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/component_launcher.h"

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/io.h>
#include <zircon/processargs.h>

#include "src/lib/fxl/logging.h"
#include "lib/sys/cpp/service_directory.h"
#include "src/developer/debug/shared/component_utils.h"
#include "src/developer/debug/shared/logging/logging.h"

namespace debug_agent {

namespace {

uint64_t kNextComponentId = 1;

// Attemps to link a zircon socket into the new component's file descriptor
// number represented by |fd|. If successful, the socket will be connected and
// a (one way) communication channel with that file descriptor will be made.
zx::socket AddStdio(int fd, fuchsia::sys::LaunchInfo* launch_info) {
  zx::socket local;
  zx::socket target;

  zx_status_t status = zx::socket::create(0, &local, &target);
  if (status != ZX_OK)
    return zx::socket();

  auto io = fuchsia::sys::FileDescriptor::New();
  io->type0 = PA_HND(PA_FD, fd);
  io->handle0 = std::move(target);

  if (fd == STDOUT_FILENO) {
    launch_info->out = std::move(io);
  } else if (fd == STDERR_FILENO) {
    launch_info->err = std::move(io);
  } else {
    FXL_NOTREACHED() << "Invalid file descriptor: " << fd;
    return zx::socket();
  }

  return local;
}

}  // namespace

ComponentLauncher::ComponentLauncher(
    std::shared_ptr<sys::ServiceDirectory> services)
   : services_(std::move(services)) {}

zx_status_t ComponentLauncher::Prepare(std::vector<std::string> argv,
                                       ComponentDescription* description,
                                       ComponentHandles* handles) {
  FXL_DCHECK(services_);
  FXL_DCHECK(!argv.empty());

  auto pkg_url = argv.front();
  debug_ipc::ComponentDescription url_desc;
  if (!debug_ipc::ExtractComponentFromPackageUrl(pkg_url, &url_desc)) {
    FXL_LOG(WARNING) << "Invalid package url: " << pkg_url;
    return ZX_ERR_INVALID_ARGS;
  }

  // Prepare the launch info. The parameters to the component do not include
  // the component URL.
  launch_info_.url = argv.front();
  for (size_t i = 1; i < argv.size(); i++)
    launch_info_.arguments->push_back(std::move(argv[i]));

  *description = {};
  description->component_id = kNextComponentId++;
  description->url = pkg_url;
  description->process_name = url_desc.component_name;
  description->filter = url_desc.component_name;

  *handles = {};
  handles->out = AddStdio(STDOUT_FILENO, &launch_info_);
  handles->err = AddStdio(STDERR_FILENO, &launch_info_);

  return ZX_OK;
};

fuchsia::sys::ComponentControllerPtr ComponentLauncher::Launch() {
  FXL_DCHECK(services_);

  fuchsia::sys::LauncherSyncPtr launcher;
  services_->Connect(launcher.NewRequest());

  // Controller is a way to manage the newly created component. We need it in
  // order to receive the terminated events. Sadly, there is no component
  // started event. This also makes us need an async::Loop so that the fidl
  // plumbing can work.
  fuchsia::sys::ComponentControllerPtr controller;
  launcher->CreateComponent(std::move(launch_info_), controller.NewRequest());

  return controller;
}

}  // namespace debug_agent
