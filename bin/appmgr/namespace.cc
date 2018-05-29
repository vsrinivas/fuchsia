// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/namespace.h"

#include <fdio/util.h>
#include <fuchsia/process/cpp/fidl.h>

#include <utility>

#include "garnet/bin/appmgr/realm.h"
#include "lib/app/cpp/environment_services.h"

namespace component {

Namespace::Namespace(fxl::RefPtr<Namespace> parent, Realm* realm,
                     ServiceListPtr service_list)
    : parent_(parent), realm_(realm) {
  component::ServiceProviderPtr services_backend;
  if (parent_) {
    parent_->services().AddBinding(services_backend.NewRequest());
  }
  services_.set_backend(std::move(services_backend));

  services_.AddService<Environment>(
      [this](fidl::InterfaceRequest<Environment> request) {
        environment_bindings_.AddBinding(this, std::move(request));
      });
  services_.AddService<ApplicationLauncher>(
      [this](fidl::InterfaceRequest<ApplicationLauncher> request) {
        launcher_bindings_.AddBinding(this, std::move(request));
      });
  services_.AddService<fuchsia::process::Launcher>(
      [this](fidl::InterfaceRequest<fuchsia::process::Launcher> request) {
        ConnectToEnvironmentService(std::move(request));
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

Namespace::~Namespace() {}

void Namespace::AddBinding(fidl::InterfaceRequest<Environment> environment) {
  environment_bindings_.AddBinding(this, std::move(environment));
}

void Namespace::CreateNestedEnvironment(
    zx::channel host_directory, fidl::InterfaceRequest<Environment> environment,
    fidl::InterfaceRequest<EnvironmentController> controller,
    fidl::StringPtr label) {
  realm_->CreateNestedJob(std::move(host_directory), std::move(environment),
                          std::move(controller), label);
}

void Namespace::GetApplicationLauncher(
    fidl::InterfaceRequest<ApplicationLauncher> launcher) {
  launcher_bindings_.AddBinding(this, std::move(launcher));
}

void Namespace::GetServices(fidl::InterfaceRequest<ServiceProvider> services) {
  services_.AddBinding(std::move(services));
}

void Namespace::GetDirectory(zx::channel directory_request) {
  services_.ServeDirectory(std::move(directory_request));
}

void Namespace::CreateApplication(
    LaunchInfo launch_info,
    fidl::InterfaceRequest<ComponentController> controller) {
  realm_->CreateApplication(std::move(launch_info), std::move(controller));
}

}  // namespace component
