// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_MANAGED_ENVIRONMENT_H_
#define SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_MANAGED_ENVIRONMENT_H_

#include <fuchsia/netemul/environment/cpp/fidl.h>
#include <lib/sys/cpp/testing/enclosing_environment.h>

#include <memory>

#include "log_listener.h"
#include "managed_launcher.h"
#include "managed_logger.h"
#include "sandbox_env.h"
#include "src/lib/fxl/macros.h"
#include "virtual_devices.h"

namespace netemul {

class ManagedEnvironment : public fuchsia::netemul::environment::ManagedEnvironment {
 public:
  using EnvironmentRunningCallback = fit::closure;
  using Options = fuchsia::netemul::environment::EnvironmentOptions;
  using LaunchService = fuchsia::netemul::environment::LaunchService;
  using FManagedEnvironment = fuchsia::netemul::environment::ManagedEnvironment;
  using Ptr = std::unique_ptr<ManagedEnvironment>;
  static Ptr CreateRoot(const fuchsia::sys::EnvironmentPtr& parent,
                        const SandboxEnv::Ptr& sandbox_env, Options options);

  const SandboxEnv::Ptr& sandbox_env() const { return sandbox_env_; }

  void GetLauncher(::fidl::InterfaceRequest<::fuchsia::sys::Launcher> launcher) override;

  void CreateChildEnvironment(fidl::InterfaceRequest<FManagedEnvironment> me,
                              Options options) override;

  void ConnectToService(std::string name, zx::channel req) override;

  void AddDevice(fuchsia::netemul::environment::VirtualDevice device) override;
  void RemoveDevice(::std::string path) override;

  ManagedLauncher& launcher() { return *launcher_; }

  void SetRunningCallback(EnvironmentRunningCallback cb) { running_callback_ = std::move(cb); }

  void Bind(fidl::InterfaceRequest<FManagedEnvironment> req);

 protected:
  friend ManagedLauncher;

  sys::testing::EnclosingEnvironment& environment();
  ManagedLoggerCollection& loggers();
  zx::channel OpenVdevDirectory();

 private:
  ManagedEnvironment(const SandboxEnv::Ptr& sandbox_env);
  void Create(const fuchsia::sys::EnvironmentPtr& parent, Options options,
              const ManagedEnvironment* managed_parent = nullptr);

  SandboxEnv::Ptr sandbox_env_;
  std::unique_ptr<sys::testing::EnclosingEnvironment> env_;
  std::unique_ptr<LogListener> log_listener_;
  std::unique_ptr<ManagedLoggerCollection> loggers_;
  std::unique_ptr<ManagedLauncher> launcher_;
  EnvironmentRunningCallback running_callback_;
  fidl::BindingSet<FManagedEnvironment> bindings_;
  std::vector<ManagedEnvironment::Ptr> children_;
  std::vector<LaunchService> service_config_;
  VirtualDevices virtual_devices_;
  std::vector<fidl::InterfaceRequest<FManagedEnvironment>> pending_requests_;
  bool ready_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ManagedEnvironment);
};

}  // namespace netemul

#endif  // SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_MANAGED_ENVIRONMENT_H_
