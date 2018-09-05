// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/component/cpp/testing/enclosing_environment.h"

#include <fuchsia/sys/cpp/fidl.h>
#include "lib/component/cpp/testing/test_util.h"
#include "lib/fidl/cpp/clone.h"
#include "lib/fxl/logging.h"

namespace component {
namespace testing {

EnclosingEnvironment::EnclosingEnvironment(
    const std::string& label, fuchsia::sys::EnvironmentPtr parent_env,
    const fbl::RefPtr<fs::Service>& loader_service)
    : running_(false),
      label_(label),
      parent_env_(std::move(parent_env)),
      svc_(fbl::AdoptRef(new fs::PseudoDir())),
      vfs_(async_get_default_dispatcher()) {
  if (loader_service) {
    AddService(loader_service, fuchsia::sys::Loader::Name_);
  } else {
    AllowParentService(fuchsia::sys::Loader::Name_);
  }

  // Environment is started by Launch().
}

std::unique_ptr<EnclosingEnvironment> EnclosingEnvironment::Create(
    const std::string& label, fuchsia::sys::EnvironmentPtr parent_env) {
  auto* env = new EnclosingEnvironment(label, std::move(parent_env), nullptr);
  return std::unique_ptr<EnclosingEnvironment>(env);
}

std::unique_ptr<EnclosingEnvironment>
EnclosingEnvironment::CreateWithCustomLoader(
    const std::string& label, fuchsia::sys::EnvironmentPtr parent_env,
    const fbl::RefPtr<fs::Service>& loader_service) {
  auto* env = new EnclosingEnvironment(label, std::move(parent_env),
                                       loader_service);
  return std::unique_ptr<EnclosingEnvironment>(env);
}

void EnclosingEnvironment::Launch() {
  FXL_DCHECK(!service_provider_)
      << "EnclosingEnvironment::Launch() called twice";

  // Connect to parent service.
  parent_env_->GetServices(parent_svc_.NewRequest());

  // Start environment with services.
  fuchsia::sys::ServiceListPtr service_list(new fuchsia::sys::ServiceList);
  service_list->names = std::move(svc_names_);
  service_list->host_directory = OpenAsDirectory(&vfs_, svc_);
  fuchsia::sys::EnvironmentPtr env;
  parent_env_->CreateNestedEnvironment(
      env.NewRequest(), env_controller_.NewRequest(), label_, zx::channel(),
      std::move(service_list), /*inherit_parent_services=*/false);
  env_controller_.set_error_handler([this] { running_ = false; });
  // Connect to launcher
  env->GetLauncher(launcher_.NewRequest());

  // Connect to service
  env->GetServices(service_provider_.NewRequest());

  env_controller_.events().OnCreated = [this]() { running_ = true; };
}

EnclosingEnvironment::~EnclosingEnvironment() {
  auto channel = env_controller_.Unbind();
  if (channel) {
    fuchsia::sys::EnvironmentControllerSyncPtr controller;
    controller.Bind(std::move(channel));
    controller->Kill();
  }
}

void EnclosingEnvironment::Kill(std::function<void()> callback) {
  env_controller_->Kill([this, callback = std::move(callback)]() {
    if (callback) {
      callback();
    }
  });
}

std::unique_ptr<EnclosingEnvironment>
EnclosingEnvironment::CreateNestedEnclosingEnvironment(std::string& label) {
  fuchsia::sys::EnvironmentPtr env;
  service_provider_->ConnectToService(fuchsia::sys::Environment::Name_,
                                      env.NewRequest().TakeChannel());
  return Create(std::move(label), std::move(env));
}

zx_status_t EnclosingEnvironment::AddServiceWithLaunchInfo(
    fuchsia::sys::LaunchInfo launch_info, const std::string& service_name) {
  FXL_DCHECK(!service_provider_) << kCannotAddServiceAfterLaunch;
  auto child = fbl::AdoptRef(
      new fs::Service([this, service_name, launch_info = std::move(launch_info),
                       controller = fuchsia::sys::ComponentControllerPtr()](
                          zx::channel client_handle) mutable {
        auto it = services_.find(launch_info.url);
        if (it == services_.end()) {
          Services services;

          fuchsia::sys::LaunchInfo dup_launch_info;
          dup_launch_info.url = launch_info.url;
          fidl::Clone(launch_info.arguments, &dup_launch_info.arguments);
          dup_launch_info.directory_request = services.NewRequest();

          CreateComponent(std::move(dup_launch_info), controller.NewRequest());
          controller.set_error_handler(
              [this, url = launch_info.url, &controller] {
                // TODO: show error? where on stderr?
                controller.Unbind();  // kills the singleton application
                services_.erase(url);
              });

          std::tie(it, std::ignore) =
              services_.emplace(launch_info.url, std::move(services));
        }

        it->second.ConnectToService(std::move(client_handle), service_name);
        return ZX_OK;
      }));
  svc_names_.push_back(service_name);
  return svc_->AddEntry(service_name, std::move(child));
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

}  // namespace testing
}  // namespace component
