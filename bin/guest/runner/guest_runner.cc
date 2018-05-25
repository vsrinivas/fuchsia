// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/runner/guest_runner.h"

#include <memory>

#include "lib/app/cpp/environment_services.h"
#include "lib/fxl/logging.h"

namespace guest_runner {

GuestRunner::GuestRunner()
    : context_(component::ApplicationContext::CreateFromStartupInfo()) {
  context_->environment()->GetApplicationLauncher(launcher_.NewRequest());
  context_->outgoing().AddPublicService<component::Runner>(
      [this](fidl::InterfaceRequest<component::Runner> request) {
        bindings_.AddBinding(this, std::move(request));
      });
}

GuestRunner::~GuestRunner() = default;

void GuestRunner::StartComponent(
    component::Package application, component::StartupInfo startup_info,
    ::fidl::InterfaceRequest<component::ComponentController> controller) {
  component::LaunchInfo launch_info;

  // Pass-through our arguments directly to the vmm package.
  launch_info.url = "vmm";
  launch_info.arguments = std::move(startup_info.launch_info.arguments);

  // Expose the specific guest package under the /guest namespace.
  launch_info.flat_namespace = component::FlatNamespace::New();
  for (size_t i = 0; i < startup_info.flat_namespace.paths->size(); ++i) {
    const auto& path = (*startup_info.flat_namespace.paths)[i];
    if (path == "/pkg") {
      launch_info.flat_namespace->paths.push_back("/guest");
      launch_info.flat_namespace->directories.push_back(
          std::move((*startup_info.flat_namespace.directories)[i]));
    }
  }

  // Pass-through our directory request.
  launch_info.directory_request =
      std::move(startup_info.launch_info.directory_request);

  launcher_->CreateApplication(std::move(launch_info), std::move(controller));
}

}  // namespace guest_runner
