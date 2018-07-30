// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/namespace.h"

#include <fuchsia/process/cpp/fidl.h>
#include <lib/async/default.h>
#include <lib/fdio/util.h>

#include <utility>

#include "garnet/bin/appmgr/job_provider_impl.h"
#include "garnet/bin/appmgr/realm.h"
#include "garnet/bin/appmgr/util.h"
#include "lib/component/cpp/environment_services.h"

namespace component {

Namespace::Namespace(fxl::RefPtr<Namespace> parent, Realm* realm,
                     fuchsia::sys::ServiceListPtr additional_services)
    : vfs_(async_get_default_dispatcher()),
      services_(fbl::AdoptRef(new ServiceProviderDirImpl())),
      job_provider_(fbl::AdoptRef(new JobProviderImpl(realm))),
      parent_(parent),
      realm_(realm) {
  if (parent_) {
    services_->set_parent(parent_->services());
  }

  services_->AddService(
      fbl::AdoptRef(new fs::Service([this](zx::channel channel) {
        environment_bindings_.AddBinding(
            this, fidl::InterfaceRequest<fuchsia::sys::Environment>(
                      std::move(channel)));
        return ZX_OK;
      })),
      fuchsia::sys::Environment::Name_);
  services_->AddService(
      fbl::AdoptRef(new fs::Service([this](zx::channel channel) {
        launcher_bindings_.AddBinding(
            this, fidl::InterfaceRequest<Launcher>(std::move(channel)));
        return ZX_OK;
      })),
      Launcher::Name_);
  services_->AddService(
      fbl::AdoptRef(new fs::Service([this](zx::channel channel) {
        component::ConnectToEnvironmentService(
            fidl::InterfaceRequest<fuchsia::process::Launcher>(
                std::move(channel)));
        return ZX_OK;
      })),
      fuchsia::process::Launcher::Name_);

  if (additional_services) {
    auto& names = additional_services->names;
    additional_services_ = additional_services->provider.Bind();
    for (auto& name : *names) {
      services_->AddService(
          fbl::AdoptRef(new fs::Service([this, name](zx::channel channel) {
            additional_services_->ConnectToService(name, std::move(channel));
            return ZX_OK;
          })),
          name);
    }
  }
}

Namespace::~Namespace() {}

void Namespace::AddBinding(
    fidl::InterfaceRequest<fuchsia::sys::Environment> environment) {
  environment_bindings_.AddBinding(this, std::move(environment));
}

void Namespace::CreateNestedEnvironment(
    zx::channel host_directory,
    fidl::InterfaceRequest<fuchsia::sys::Environment> environment,
    fidl::InterfaceRequest<fuchsia::sys::EnvironmentController> controller,
    fidl::StringPtr label) {
  realm_->CreateNestedJob(std::move(host_directory), std::move(environment),
                          std::move(controller), label);
}

void Namespace::GetLauncher(fidl::InterfaceRequest<Launcher> launcher) {
  launcher_bindings_.AddBinding(this, std::move(launcher));
}

void Namespace::GetServices(
    fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> services) {
  services_->AddBinding(std::move(services));
}

zx_status_t Namespace::ServeServiceDirectory(zx::channel directory_request) {
  return vfs_.ServeDirectory(services_, std::move(directory_request));
}

void Namespace::CreateComponent(
    fuchsia::sys::LaunchInfo launch_info,
    fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller) {
  realm_->CreateComponent(std::move(launch_info), std::move(controller));
}

zx::channel Namespace::OpenServicesAsDirectory() {
  return Util::OpenAsDirectory(&vfs_, services_);
}

void Namespace::SetServicesWhitelist(
    const std::vector<std::string>& services) {
  services_->SetServicesWhitelist(services);
}

}  // namespace component
