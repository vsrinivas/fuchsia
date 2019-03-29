// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/tests/maxwell_integration/test.h"

#include <src/lib/fxl/logging.h>

namespace maxwell {
namespace {

constexpr char kEnvironmentLabel[] = "maxwell-test-env";

}

MaxwellTestBase::MaxwellTestBase() : loop_(&kAsyncLoopConfigAttachToThread) {
  startup_context_ = component::StartupContext::CreateFromStartupInfo();

  child_app_services_.AddService<fuchsia::modular::ComponentContext>(
      [this](
          fidl::InterfaceRequest<fuchsia::modular::ComponentContext> request) {
        child_component_context_.Connect(std::move(request));
      });
}

void MaxwellTestBase::StartAgent(
    const std::string& url,
    std::unique_ptr<MaxwellServiceProviderBridge> bridge) {
  bridge_ = std::move(bridge);

  fuchsia::sys::ServiceListPtr service_list(new fuchsia::sys::ServiceList);
  service_list->names = bridge_->service_names();
  service_list->host_directory = bridge_->OpenAsDirectory();
  fuchsia::sys::EnvironmentPtr agent_env;
  startup_context_->environment()->CreateNestedEnvironment(
      agent_env.NewRequest(), environment_controller_.NewRequest(),
      kEnvironmentLabel, std::move(service_list), {});

  fuchsia::sys::LauncherPtr launcher;
  agent_env->GetLauncher(launcher.NewRequest());

  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = url;
  component::Services services;
  launch_info.directory_request = services.NewRequest();
  FXL_LOG(INFO) << "Starting modular agent " << url;
  launcher->CreateComponent(std::move(launch_info), nullptr);
}

component::Services MaxwellTestBase::StartServices(const std::string& url) {
  component::Services services;
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = url;
  launch_info.directory_request = services.NewRequest();

  auto service_list = fuchsia::sys::ServiceList::New();
  service_list->names.push_back(fuchsia::modular::ComponentContext::Name_);
  child_app_services_.AddBinding(service_list->provider.NewRequest());
  launch_info.additional_services = std::move(service_list);

  fuchsia::sys::ComponentControllerPtr component_ptr;
  startup_context_->launcher()->CreateComponent(std::move(launch_info),
                                                component_ptr.NewRequest());
  component_ptrs_.push_back(std::move(component_ptr));
  return services;
}

fuchsia::sys::Environment* MaxwellTestBase::root_environment() {
  return startup_context_->environment().get();
}

}  // namespace maxwell
