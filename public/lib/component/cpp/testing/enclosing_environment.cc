// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/component/cpp/testing/enclosing_environment.h"

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fit/function.h>

#include "lib/component/cpp/testing/test_util.h"
#include "lib/fidl/cpp/clone.h"
#include "lib/fxl/logging.h"

namespace component {
namespace testing {

EnvironmentServices::EnvironmentServices(
    const fuchsia::sys::EnvironmentPtr& parent_env,
    const fbl::RefPtr<fs::Service>& loader_service,
    async_dispatcher_t* dispatcher)
    : vfs_(std::make_unique<fs::SynchronousVfs>(
          dispatcher == nullptr ? async_get_default_dispatcher() : dispatcher)),
      svc_(fbl::AdoptRef(new fs::PseudoDir())) {
  parent_env->GetServices(parent_svc_.NewRequest());
  if (loader_service) {
    AddService(loader_service, fuchsia::sys::Loader::Name_);
  } else {
    AllowParentService(fuchsia::sys::Loader::Name_);
  }
}

// static
std::unique_ptr<EnvironmentServices> EnvironmentServices::Create(
    const fuchsia::sys::EnvironmentPtr& parent_env,
    async_dispatcher_t* dispatcher) {
  return std::unique_ptr<EnvironmentServices>(
      new EnvironmentServices(parent_env, nullptr, dispatcher));
}

// static
std::unique_ptr<EnvironmentServices>
EnvironmentServices::CreateWithCustomLoader(
    const fuchsia::sys::EnvironmentPtr& parent_env,
    const fbl::RefPtr<fs::Service>& loader_service,
    async_dispatcher_t* dispatcher) {
  return std::unique_ptr<EnvironmentServices>(
      new EnvironmentServices(parent_env, loader_service, dispatcher));
}

zx_status_t EnvironmentServices::AddService(
    const fbl::RefPtr<fs::Service> service, const std::string& service_name) {
  svc_names_.push_back(service_name);
  return svc_->AddEntry(service_name.c_str(), service);
}

zx_status_t EnvironmentServices::AddServiceWithLaunchInfo(
    fuchsia::sys::LaunchInfo launch_info, const std::string& service_name) {
  return AddServiceWithLaunchInfo(
      launch_info.url,
      [launch_info = std::move(launch_info)]() {
        // clone only URL and Arguments
        fuchsia::sys::LaunchInfo dup_launch_info;
        fidl::Clone(launch_info.url, &dup_launch_info.url);
        fidl::Clone(launch_info.arguments, &dup_launch_info.arguments);
        return dup_launch_info;
      },
      service_name);
}

zx_status_t EnvironmentServices::AddServiceWithLaunchInfo(
    std::string singleton_id, fit::function<fuchsia::sys::LaunchInfo()> handler,
    const std::string& service_name) {
  auto child = fbl::AdoptRef(
      new fs::Service([this, service_name, handler = std::move(handler),
                       singleton_id = std::move(singleton_id),
                       controller = fuchsia::sys::ComponentControllerPtr()](
                          zx::channel client_handle) mutable {
        auto it = singleton_services_.find(singleton_id);
        if (it == singleton_services_.end()) {
          component::Services services;

          fuchsia::sys::LaunchInfo launch_info = handler();
          launch_info.directory_request = services.NewRequest();

          enclosing_env_->CreateComponent(std::move(launch_info),
                                          controller.NewRequest());
          controller.set_error_handler(
              [this, singleton_id, &controller](zx_status_t status) {
                // TODO: show error? where on stderr?
                controller.Unbind();  // kills the singleton application
                singleton_services_.erase(singleton_id);
              });

          std::tie(it, std::ignore) =
              singleton_services_.emplace(singleton_id, std::move(services));
        }

        it->second.ConnectToService(std::move(client_handle), service_name);
        return ZX_OK;
      }));
  svc_names_.push_back(service_name);
  return svc_->AddEntry(service_name, std::move(child));
}

zx_status_t EnvironmentServices::AllowParentService(
    const std::string& service_name) {
  svc_names_.push_back(service_name);
  return svc_->AddEntry(
      service_name.c_str(),
      fbl::AdoptRef(new fs::Service([this, service_name](zx::channel channel) {
        parent_svc_->ConnectToService(service_name, std::move(channel));
        return ZX_OK;
      })));
}

EnclosingEnvironment::EnclosingEnvironment(
    const std::string& label, const fuchsia::sys::EnvironmentPtr& parent_env,
    std::unique_ptr<EnvironmentServices> services,
    const fuchsia::sys::EnvironmentOptions& options)
    : label_(label), services_(std::move(services)) {
  services_->set_enclosing_env(this);

  // Start environment with services.
  fuchsia::sys::ServiceListPtr service_list(new fuchsia::sys::ServiceList);
  service_list->names = std::move(services_->svc_names_);
  service_list->host_directory =
      OpenAsDirectory(services_->vfs_.get(), services_->svc_);
  fuchsia::sys::EnvironmentPtr env;

  parent_env->CreateNestedEnvironment(env.NewRequest(),
                                      env_controller_.NewRequest(), label_,
                                      std::move(service_list), options);
  env_controller_.set_error_handler(
      [this](zx_status_t status) { SetRunning(false); });
  // Connect to launcher
  env->GetLauncher(launcher_.NewRequest());

  // Connect to service
  env->GetServices(service_provider_.NewRequest());

  env_controller_.events().OnCreated = [this]() { SetRunning(true); };
}

// static
std::unique_ptr<EnclosingEnvironment> EnclosingEnvironment::Create(
    const std::string& label, const fuchsia::sys::EnvironmentPtr& parent_env,
    std::unique_ptr<EnvironmentServices> services,
    const fuchsia::sys::EnvironmentOptions& options) {
  auto* env =
      new EnclosingEnvironment(label, parent_env, std::move(services), options);
  return std::unique_ptr<EnclosingEnvironment>(env);
}

EnclosingEnvironment::~EnclosingEnvironment() {
  auto channel = env_controller_.Unbind();
  if (channel) {
    fuchsia::sys::EnvironmentControllerSyncPtr controller;
    controller.Bind(std::move(channel));
    controller->Kill();
  }
}

void EnclosingEnvironment::Kill(fit::function<void()> callback) {
  env_controller_->Kill([this, callback = std::move(callback)]() {
    if (callback) {
      callback();
    }
  });
}

std::unique_ptr<EnclosingEnvironment>
EnclosingEnvironment::CreateNestedEnclosingEnvironment(
    const std::string& label) {
  fuchsia::sys::EnvironmentPtr env;
  service_provider_->ConnectToService(fuchsia::sys::Environment::Name_,
                                      env.NewRequest().TakeChannel());
  return Create(label, env, EnvironmentServices::Create(env));
}

void EnclosingEnvironment::CreateComponent(
    fuchsia::sys::LaunchInfo launch_info,
    fidl::InterfaceRequest<fuchsia::sys::ComponentController> request) {
  launcher_.CreateComponent(std::move(launch_info), std::move(request));
}

fuchsia::sys::ComponentControllerPtr EnclosingEnvironment::CreateComponent(
    fuchsia::sys::LaunchInfo launch_info) {
  fuchsia::sys::ComponentControllerPtr controller;
  CreateComponent(std::move(launch_info), controller.NewRequest());
  return controller;
}

fuchsia::sys::ComponentControllerPtr
EnclosingEnvironment::CreateComponentFromUrl(std::string component_url) {
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = component_url;

  return CreateComponent(std::move(launch_info));
}

void EnclosingEnvironment::SetRunning(bool running) {
  if (running_ != running) {
    running_ = running;
    if (running_changed_callback_) {
      running_changed_callback_(running_);
    }
  }
}

}  // namespace testing
}  // namespace component
