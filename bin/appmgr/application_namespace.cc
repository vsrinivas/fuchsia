// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/application_namespace.h"

#include <utility>

#include "garnet/bin/appmgr/job_holder.h"

namespace component {

ApplicationNamespace::ApplicationNamespace(
    fxl::RefPtr<ApplicationNamespace> parent,
    JobHolder* job_holder,
    ServiceListPtr service_list)
    : parent_(parent), job_holder_(job_holder) {
  component::ServiceProviderPtr services_backend;
  if (parent_) {
    parent_->services().AddBinding(services_backend.NewRequest());
  }
  services_.set_backend(std::move(services_backend));

  services_.AddService<ApplicationEnvironment>(
      [this](fidl::InterfaceRequest<ApplicationEnvironment> request) {
        environment_bindings_.AddBinding(this, std::move(request));
      });
  services_.AddService<ApplicationLauncher>(
      [this](fidl::InterfaceRequest<ApplicationLauncher> request) {
        launcher_bindings_.AddBinding(this, std::move(request));
      });

  if (service_list) {
    auto& names = service_list->names;
    additional_services_ = service_list->provider.Bind();
    for (auto& name : *names) {
      services_.AddServiceForName(
          [this, name](zx::channel channel) {
            additional_services_->ConnectToService(name, std::move(channel));
          },
          name);
    }
  }
}

ApplicationNamespace::~ApplicationNamespace() {}

void ApplicationNamespace::AddBinding(
    fidl::InterfaceRequest<ApplicationEnvironment> environment) {
  environment_bindings_.AddBinding(this, std::move(environment));
}

void ApplicationNamespace::CreateNestedEnvironment(
    zx::channel host_directory,
    fidl::InterfaceRequest<ApplicationEnvironment> environment,
    fidl::InterfaceRequest<ApplicationEnvironmentController> controller,
    fidl::StringPtr label) {
  job_holder_->CreateNestedJob(std::move(host_directory),
                               std::move(environment), std::move(controller),
                               label);
}

void ApplicationNamespace::GetApplicationLauncher(
    fidl::InterfaceRequest<ApplicationLauncher> launcher) {
  launcher_bindings_.AddBinding(this, std::move(launcher));
}

void ApplicationNamespace::GetServices(
    fidl::InterfaceRequest<ServiceProvider> services) {
  services_.AddBinding(std::move(services));
}

void ApplicationNamespace::GetDirectory(zx::channel directory_request) {
  services_.ServeDirectory(std::move(directory_request));
}

void ApplicationNamespace::CreateApplication(
    ApplicationLaunchInfo launch_info,
    fidl::InterfaceRequest<ApplicationController> controller) {
  job_holder_->CreateApplication(std::move(launch_info), std::move(controller));
}

}  // namespace component
