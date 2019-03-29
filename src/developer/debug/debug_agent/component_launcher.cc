// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/component_launcher.h"

#include <fuchsia/sys/cpp/fidl.h>

#include "src/lib/fxl/logging.h"
#include "lib/sys/cpp/service_directory.h"
#include "src/developer/debug/ipc/debug/logging.h"
#include "src/developer/debug/shared/component_utils.h"

namespace debug_agent {

ComponentLauncher::ComponentLauncher(
    std::shared_ptr<sys::ServiceDirectory> services)
   : services_(std::move(services)) {}

zx_status_t ComponentLauncher::Prepare(std::vector<std::string> argv,
                                       LaunchComponentDescription* out) {
  FXL_DCHECK(services_);

  auto& pkg_url = argv.front();
  debug_ipc::ComponentDescription desc;
  if (!debug_ipc::ExtractComponentFromPackageUrl(pkg_url, &desc)) {
    FXL_LOG(WARNING) << "Invalid package url: " << pkg_url;
    return ZX_ERR_INVALID_ARGS;
  }

  argv_ = std::move(argv);

  desc_.url = pkg_url;
  desc_.process_name = desc.component_name;
  desc_.filter = desc.component_name;

  *out = desc_;
  return ZX_OK;
};

fuchsia::sys::ComponentControllerPtr ComponentLauncher::Launch() {
  FXL_DCHECK(services_);

  auto& pkg_url = argv_.front();
  DEBUG_LOG() << "Launching component. Url: " << pkg_url
              << ", name: " << desc_.process_name
              << ", filter: " << desc_.filter;

  fuchsia::sys::LaunchInfo launch_info = {};
  launch_info.url = pkg_url;
  for (size_t i = 1; i < argv_.size(); i++) {
    launch_info.arguments.push_back(argv_[i]);
  }

  fuchsia::sys::LauncherSyncPtr launcher;
  services_->Connect(launcher.NewRequest());

  // Controller is a way to manage the newly created component. We need it in
  // order to receive the terminated events. Sadly, there is no component
  // started event. This also makes us need an async::Loop so that the fidl
  // plumbing can work.
  fuchsia::sys::ComponentControllerPtr controller;
  launcher->CreateComponent(std::move(launch_info), controller.NewRequest());

  return controller;
}

}  // namespace debug_agent
