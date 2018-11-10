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

namespace component {

Namespace::Namespace(fxl::RefPtr<Namespace> parent, Realm* realm,
                     fuchsia::sys::ServiceListPtr additional_services,
                     const std::vector<std::string>* service_whitelist)
    : vfs_(async_get_default_dispatcher()),
      services_(fbl::AdoptRef(new ServiceProviderDirImpl(service_whitelist))),
      job_provider_(fbl::AdoptRef(new JobProviderImpl(realm))),
      realm_(realm) {
  services_->AddService(
      fuchsia::sys::Environment::Name_,
      fbl::AdoptRef(new fs::Service([this](zx::channel channel) {
        environment_bindings_.AddBinding(
            this, fidl::InterfaceRequest<fuchsia::sys::Environment>(
                      std::move(channel)));
        return ZX_OK;
      })));
  services_->AddService(
      Launcher::Name_,
      fbl::AdoptRef(new fs::Service([this](zx::channel channel) {
        launcher_bindings_.AddBinding(
            this, fidl::InterfaceRequest<Launcher>(std::move(channel)));
        return ZX_OK;
      })));
  services_->AddService(
      fuchsia::process::Launcher::Name_,
      fbl::AdoptRef(new fs::Service([this](zx::channel channel) {
        realm_->environment_services()->ConnectToService(
            fidl::InterfaceRequest<fuchsia::process::Launcher>(
                std::move(channel)));
        return ZX_OK;
      })));
  services_->AddService(
      fuchsia::process::Resolver::Name_,
      fbl::AdoptRef(new fs::Service([this](zx::channel channel) {
        resolver_bindings_.AddBinding(
            this, fidl::InterfaceRequest<fuchsia::process::Resolver>(
                      std::move(channel)));
        return ZX_OK;
      })));

  if (additional_services) {
    auto& names = additional_services->names;
    service_provider_ = additional_services->provider.Bind();
    service_host_directory_ = std::move(additional_services->host_directory);
    for (auto& name : *names) {
      if (service_host_directory_) {
        services_->AddService(
            name,
            fbl::AdoptRef(new fs::Service([this, name](zx::channel channel) {
              fdio_service_connect_at(service_host_directory_.get(),
                                      name->c_str(), channel.release());
              return ZX_OK;
            })));
      } else {
        services_->AddService(
            name,
            fbl::AdoptRef(new fs::Service([this, name](zx::channel channel) {
              service_provider_->ConnectToService(name, std::move(channel));
              return ZX_OK;
            })));
      }
    }
  }

  // If any services in |parent| share a name with |additional_services|,
  // |additional_services| takes priority.
  if (parent) {
    services_->set_parent(parent->services());
  }
}

Namespace::~Namespace() {}

void Namespace::AddBinding(
    fidl::InterfaceRequest<fuchsia::sys::Environment> environment) {
  environment_bindings_.AddBinding(this, std::move(environment));
}

void Namespace::CreateNestedEnvironment(
    fidl::InterfaceRequest<fuchsia::sys::Environment> environment,
    fidl::InterfaceRequest<fuchsia::sys::EnvironmentController> controller,
    fidl::StringPtr label, fuchsia::sys::ServiceListPtr additional_services,
    fuchsia::sys::EnvironmentOptions options) {
  realm_->CreateNestedEnvironment(std::move(environment), std::move(controller),
                                  label, std::move(additional_services),
                                  options);
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

void Namespace::Resolve(fidl::StringPtr name,
                        fuchsia::process::Resolver::ResolveCallback callback) {
  realm_->Resolve(name, std::move(callback));
}

}  // namespace component
