// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/test_runner/cpp/scope.h"

namespace test_runner {

Scope::Scope(const app::ApplicationEnvironmentPtr& parent_env,
             const std::string& label)
    : binding_(this) {
  app::ServiceProviderPtr parent_env_service_provider;
  parent_env->GetServices(parent_env_service_provider.NewRequest());
  service_provider_impl_.SetDefaultServiceProvider(
      std::move(parent_env_service_provider));
  parent_env->CreateNestedEnvironment(binding_.NewBinding(), env_.NewRequest(),
                                      env_controller_.NewRequest(), label);
}

app::ApplicationLauncher* Scope::GetLauncher() {
  if (!env_launcher_) {
    env_->GetApplicationLauncher(env_launcher_.NewRequest());
  }
  return env_launcher_.get();
}

// |ApplicationEnvironmentHost|:
void Scope::GetApplicationEnvironmentServices(
    fidl::InterfaceRequest<app::ServiceProvider> environment_services) {
  service_provider_impl_.AddBinding(std::move(environment_services));
}

}  // namespace test_runner
