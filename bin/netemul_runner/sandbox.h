// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_NETEMUL_RUNNER_SANDBOX_H_
#define GARNET_BIN_NETEMUL_RUNNER_SANDBOX_H_

#include <fuchsia/sys/cpp/fidl.h>
#include "managed_environment.h"
#include "sandbox_env.h"

namespace netemul {

class SandboxArgs {
 public:
  std::string package;
  std::vector<std::string> args;
};

class Sandbox {
 public:
  using TerminationReason = fuchsia::sys::TerminationReason;
  using TerminationCallback =
      fit::function<void(int64_t code, TerminationReason reason)>;
  explicit Sandbox(SandboxArgs args);

  void SetTerminationCallback(TerminationCallback callback) {
    termination_callback_ = std::move(callback);
  }

  void Start();

 private:
  void LoadPackage(fuchsia::sys::PackagePtr package);
  void Terminate(TerminationReason reason);
  void Terminate(int64_t exit_code, TerminationReason reason);
  void StartRootEnvironment();

  SandboxArgs args_;
  SandboxEnv::Ptr sandbox_env_;
  TerminationCallback termination_callback_;
  fuchsia::sys::EnvironmentPtr parent_env_;
  fuchsia::sys::LoaderPtr loader_;
  ManagedEnvironment::Ptr root_;
  fuchsia::sys::ComponentControllerPtr root_proc_;
};

}  // namespace netemul

#endif  // GARNET_BIN_NETEMUL_RUNNER_SANDBOX_H_
