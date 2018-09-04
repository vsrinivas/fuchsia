// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/fidl/environment.h"

#include <lib/fxl/logging.h>

namespace modular {

Environment::Environment(const fuchsia::sys::EnvironmentPtr& parent_env,
                         const std::string& label) {
  InitEnvironment(parent_env, label);
}

Environment::Environment(const Environment* const parent_scope,
                         const std::string& label) {
  FXL_DCHECK(parent_scope != nullptr);
  InitEnvironment(parent_scope->environment(), label);
}

fuchsia::sys::Launcher* Environment::GetLauncher() {
  if (!env_launcher_) {
    env_->GetLauncher(env_launcher_.NewRequest());
  }
  return env_launcher_.get();
}

void Environment::InitEnvironment(
    const fuchsia::sys::EnvironmentPtr& parent_env, const std::string& label) {
  zx::channel h1, h2;
  if (zx::channel::create(0, &h1, &h2) < 0)
    return;
  parent_env->GetDirectory(std::move(h1));
  service_provider_bridge_.set_backing_dir(std::move(h2));
  parent_env->CreateNestedEnvironment(
      env_.NewRequest(), env_controller_.NewRequest(), label,
      service_provider_bridge_.OpenAsDirectory(),
      /*additional_services=*/nullptr, /*inherit_parent_services=*/false);
}

}  // namespace modular
