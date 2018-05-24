// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/test_runner/cpp/scope.h"

namespace test_runner {

Scope::Scope(const component::EnvironmentPtr& parent_env,
             const std::string& label) {
  zx::channel h1, h2;
  if (zx::channel::create(0, &h1, &h2) < 0)
    return;
  parent_env->GetDirectory(std::move(h1));
  service_provider_bridge_.set_backing_dir(std::move(h2));
  parent_env->CreateNestedEnvironment(
      service_provider_bridge_.OpenAsDirectory(), env_.NewRequest(),
      env_controller_.NewRequest(), label);
}

component::ApplicationLauncher* Scope::GetLauncher() {
  if (!env_launcher_) {
    env_->GetApplicationLauncher(env_launcher_.NewRequest());
  }
  return env_launcher_.get();
}

}  // namespace test_runner
