// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_LIB_FIDL_SCOPE_H_
#define APPS_MODULAR_LIB_FIDL_SCOPE_H_

#include <memory>
#include <string>

#include "application/lib/app/application_context.h"
#include "application/lib/app/service_provider_impl.h"
#include "application/services/application_environment.fidl.h"
#include "application/services/application_environment_host.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/interface_request.h"

namespace modular {

// A simple implementation of the ApplicationEnvironmentHost that provides fate
// separation of sets of applications run by one application. The environment
// services are delegated to the parent environment.
class Scope : public app::ApplicationEnvironmentHost {
 public:
  Scope(const app::ApplicationEnvironmentPtr& parent_env,
        const std::string& label)
      : binding_(this) {
    InitScope(parent_env, label);
  }

  Scope(const Scope* const parent_scope, const std::string& label)
      : binding_(this) {
    FTL_DCHECK(parent_scope != nullptr);
    InitScope(parent_scope->environment(), label);
  }

  template <typename Interface>
  void AddService(
      app::ServiceProviderImpl::InterfaceRequestHandler<Interface> handler,
      const std::string& service_name = Interface::Name_) {
    service_provider_impl_.AddService(handler, service_name);
  }

  app::ApplicationLauncher* GetLauncher() {
    if (!env_launcher_) {
      env_->GetApplicationLauncher(env_launcher_.NewRequest());
    }
    return env_launcher_.get();
  }

  const app::ApplicationEnvironmentPtr& environment() const { return env_; }

 private:
  // |ApplicationEnvironmentHost|:
  void GetApplicationEnvironmentServices(
      fidl::InterfaceRequest<app::ServiceProvider> environment_services)
      override {
    service_provider_impl_.AddBinding(std::move(environment_services));
  }

  void InitScope(const app::ApplicationEnvironmentPtr& parent_env,
                 const std::string& label) {
    app::ServiceProviderPtr parent_env_service_provider;
    parent_env->GetServices(parent_env_service_provider.NewRequest());
    service_provider_impl_.SetDefaultServiceProvider(
        std::move(parent_env_service_provider));
    parent_env->CreateNestedEnvironment(binding_.NewBinding(),
                                        env_.NewRequest(),
                                        env_controller_.NewRequest(), label);
  }

  fidl::Binding<app::ApplicationEnvironmentHost> binding_;
  app::ApplicationEnvironmentPtr env_;
  app::ApplicationLauncherPtr env_launcher_;
  app::ApplicationEnvironmentControllerPtr env_controller_;
  app::ServiceProviderImpl service_provider_impl_;
};

}  // namespace modular

#endif  // APPS_MODULAR_LIB_FIDL_SCOPE_H_
