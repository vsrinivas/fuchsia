// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/component/cpp/testing/enclosing_environment.h"

#include "lib/component/cpp/testing/test_util.h"
#include "lib/fidl/cpp/clone.h"

namespace component {
namespace testing {

EnclosingEnvironment::EnclosingEnvironment(
    const std::string& label, const fuchsia::sys::EnvironmentPtr& parent_env,
    const fbl::RefPtr<fs::Service>& loader_service)
    : running_(false),
      svc_(fbl::AdoptRef(new fs::PseudoDir())),
      vfs_(async_get_default_dispatcher()) {
  // Connect to parent service.
  parent_env->GetServices(parent_svc_.NewRequest());

  if (loader_service) {
    AddService(loader_service, fuchsia::sys::Loader::Name_);
  } else {
    AllowParentService(fuchsia::sys::Loader::Name_);
  }

  // Create environment
  parent_env->CreateNestedEnvironment(OpenAsDirectory(&vfs_, svc_),
                                      env_.NewRequest(),
                                      env_controller_.NewRequest(), label);
  env_controller_.set_error_handler([this] { running_ = false; });
  // Connect to launcher
  env_->GetLauncher(launcher_.NewRequest());

  // Connect to service
  env_->GetServices(service_provider_.NewRequest());

  env_controller_.events().OnCreated = [this]() { running_ = true; };
}

std::unique_ptr<EnclosingEnvironment> EnclosingEnvironment::Create(
    const std::string& label, const fuchsia::sys::EnvironmentPtr& parent_env) {
  auto* env = new EnclosingEnvironment(label, parent_env, nullptr);
  return std::unique_ptr<EnclosingEnvironment>(env);
}

std::unique_ptr<EnclosingEnvironment>
EnclosingEnvironment::CreateWithCustomLoader(
    const std::string& label, const fuchsia::sys::EnvironmentPtr& parent_env,
    const fbl::RefPtr<fs::Service>& loader_service) {
  auto* env = new EnclosingEnvironment(label, parent_env, loader_service);
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

void EnclosingEnvironment::Kill(std::function<void()> callback) {
  env_controller_->Kill([this, callback = std::move(callback)]() {
    if (callback) {
      callback();
    }
  });
}

std::unique_ptr<EnclosingEnvironment>
EnclosingEnvironment::CreateNestedEnclosingEnvironment(std::string& label) {
  return Create(std::move(label), env_);
}

zx_status_t EnclosingEnvironment::AddServiceWithLaunchInfo(
    fuchsia::sys::LaunchInfo launch_info, const std::string& service_name) {
  auto child = fbl::AdoptRef(
      new fs::Service([this, service_name, launch_info = std::move(launch_info),
                       controller = fuchsia::sys::ComponentControllerPtr()](
                          zx::channel client_handle) mutable {
        auto it = services_.find(launch_info.url);
        if (it == services_.end()) {
          Services services;

          auto url = launch_info.url;
          launch_info.directory_request = services.NewRequest();

          CreateComponent(std::move(launch_info), controller.NewRequest());
          controller.set_error_handler([this, url = url, &controller] {
            controller.Unbind();  // kills the singleton application
            services_.erase(url);
          });

          std::tie(it, std::ignore) =
              services_.emplace(url, std::move(services));
        }

        it->second.ConnectToService(std::move(client_handle), service_name);
        return ZX_OK;
      }));
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
