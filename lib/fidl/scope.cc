// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/fidl/scope.h"

#include <lib/fxl/logging.h>

namespace modular {

Scope::Scope(const fuchsia::sys::EnvironmentPtr& parent_env,
             const std::string& label) {
  InitScope(parent_env, label);
}

Scope::Scope(const Scope* const parent_scope, const std::string& label) {
  FXL_DCHECK(parent_scope != nullptr);
  InitScope(parent_scope->environment(), label);
}

fuchsia::sys::Launcher* Scope::GetLauncher() {
  if (!env_launcher_) {
    env_->GetLauncher(env_launcher_.NewRequest());
  }
  return env_launcher_.get();
}

void Scope::InitScope(const fuchsia::sys::EnvironmentPtr& parent_env,
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

}  // namespace modular
