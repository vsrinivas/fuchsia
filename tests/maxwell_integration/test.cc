// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/tests/maxwell_integration/test.h"

namespace maxwell {

MaxwellTestBase::MaxwellTestBase() {
  startup_context_ = component::ApplicationContext::CreateFromStartupInfo();
  auto root_environment = startup_context_->environment().get();
  FXL_CHECK(root_environment != nullptr);

  agent_launcher_ = std::make_unique<maxwell::AgentLauncher>(root_environment);

  child_app_services_.AddService<modular::ComponentContext>(
      [this](fidl::InterfaceRequest<modular::ComponentContext> request) {
        child_component_context_.Connect(std::move(request));
      });
}

component::Services MaxwellTestBase::StartServices(const std::string& url) {
  component::Services services;
  component::ApplicationLaunchInfo launch_info;
  launch_info.url = url;
  launch_info.directory_request = services.NewRequest();

  auto service_list = component::ServiceList::New();
  service_list->names.push_back(modular::ComponentContext::Name_);
  child_app_services_.AddBinding(service_list->provider.NewRequest());
  launch_info.additional_services = std::move(service_list);

  startup_context_->launcher()->CreateApplication(std::move(launch_info),
                                                  nullptr);
  return services;
}

component::ApplicationEnvironment* MaxwellTestBase::root_environment() {
  return startup_context_->environment().get();
}

}  // namespace maxwell
