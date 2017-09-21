// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/lib/fidl/scope.h"

namespace modular {

Scope::Scope(const app::ApplicationEnvironmentPtr& parent_env,
             const std::string& label)
    : binding_(this) {
  InitScope(parent_env, label);
}

Scope::Scope(const Scope* const parent_scope, const std::string& label)
    : binding_(this) {
  FXL_DCHECK(parent_scope != nullptr);
  InitScope(parent_scope->environment(), label);
}

app::ApplicationLauncher* Scope::GetLauncher() {
  if (!env_launcher_) {
    env_->GetApplicationLauncher(env_launcher_.NewRequest());
  }
  return env_launcher_.get();
}

void Scope::GetApplicationEnvironmentServices(
    fidl::InterfaceRequest<app::ServiceProvider> environment_services) {
  service_provider_impl_.AddBinding(std::move(environment_services));
}

void Scope::InitScope(const app::ApplicationEnvironmentPtr& parent_env,
                      const std::string& label) {
  app::ServiceProviderPtr parent_env_service_provider;
  parent_env->GetServices(parent_env_service_provider.NewRequest());
  service_provider_impl_.SetDefaultServiceProvider(
      std::move(parent_env_service_provider));
  parent_env->CreateNestedEnvironment(binding_.NewBinding(), env_.NewRequest(),
                                      env_controller_.NewRequest(), label);
}

}  // namespace modular
