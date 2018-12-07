// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "managed_environment.h"

namespace netemul {

using component::testing::EnclosingEnvironment;
using component::testing::EnvironmentServices;

ManagedEnvironment::Ptr ManagedEnvironment::CreateRoot(
    const fuchsia::sys::EnvironmentPtr& parent,
    const SandboxEnv::Ptr& sandbox_env) {
  auto services = EnvironmentServices::Create(parent);

  fuchsia::sys::EnvironmentOptions options = {.kill_on_oom = true,
                                              .allow_parent_runners = false,
                                              .inherit_parent_services = true};

  auto enclosing = EnclosingEnvironment::Create("root", parent,
                                                std::move(services), options);

  return ManagedEnvironment::Ptr(
      new ManagedEnvironment(std::move(enclosing), sandbox_env));
}

ManagedEnvironment::ManagedEnvironment(
    std::unique_ptr<component::testing::EnclosingEnvironment> env,
    const SandboxEnv::Ptr& sandbox_env)
    : sandbox_env_(sandbox_env), env_(std::move(env)), launcher_(this) {
  env_->SetRunningChangedCallback([this](bool running) {
    if (running && running_callback_) {
      running_callback_();
    }
  });
}

component::testing::EnclosingEnvironment& ManagedEnvironment::environment() {
  return *env_;
}
}  // namespace netemul
