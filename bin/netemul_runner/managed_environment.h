// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_NETEMUL_RUNNER_MANAGED_ENVIRONMENT_H_
#define GARNET_BIN_NETEMUL_RUNNER_MANAGED_ENVIRONMENT_H_

#include <lib/component/cpp/testing/enclosing_environment.h>
#include <lib/svc/cpp/services.h>
#include <memory>
#include "managed_launcher.h"
#include "sandbox_env.h"

namespace netemul {

class ManagedEnvironment {
 public:
  using EnvironmentRunningCallback = fit::closure;
  using Ptr = std::unique_ptr<ManagedEnvironment>;
  static Ptr CreateRoot(const fuchsia::sys::EnvironmentPtr& parent,
                        const SandboxEnv::Ptr& sandbox_env);

  const SandboxEnv::Ptr& sandbox_env() const { return sandbox_env_; }

  const std::shared_ptr<component::Services>& services() {
    if (!services_) {
      services_ = std::make_shared<component::Services>();
    }
    return services_;
  }

  ManagedLauncher& launcher() { return launcher_; }

  void SetRunningCallback(EnvironmentRunningCallback cb) {
    running_callback_ = std::move(cb);
  }

 protected:
  friend ManagedLauncher;

  component::testing::EnclosingEnvironment& environment();

 private:
  ManagedEnvironment(
      std::unique_ptr<component::testing::EnclosingEnvironment> env,
      const SandboxEnv::Ptr& sandbox_env);

  SandboxEnv::Ptr sandbox_env_;
  std::unique_ptr<component::testing::EnclosingEnvironment> env_;
  ManagedLauncher launcher_;
  std::shared_ptr<component::Services> services_;
  EnvironmentRunningCallback running_callback_;
};

}  // namespace netemul

#endif  // GARNET_BIN_NETEMUL_RUNNER_MANAGED_ENVIRONMENT_H_
