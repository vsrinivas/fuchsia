// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/tests/maxwell_integration/test.h"

namespace maxwell {

MaxwellTestBase::MaxwellTestBase() : loop_(&kAsyncLoopConfigAttachToThread) {
  startup_context_ = component::StartupContext::CreateFromStartupInfo();
  auto root_environment = startup_context_->environment().get();
  FXL_CHECK(root_environment != nullptr);

  agent_launcher_ = std::make_unique<maxwell::AgentLauncher>(root_environment);

  child_app_services_.AddService<fuchsia::modular::ComponentContext>(
      [this](
          fidl::InterfaceRequest<fuchsia::modular::ComponentContext> request) {
        child_component_context_.Connect(std::move(request));
      });
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

  startup_context_->launcher()->CreateComponent(std::move(launch_info),
                                                nullptr);
  return services;
}

fuchsia::sys::Environment* MaxwellTestBase::root_environment() {
  return startup_context_->environment().get();
}

}  // namespace maxwell
