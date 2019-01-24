// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_NETEMUL_RUNNER_SANDBOX_H_
#define GARNET_BIN_NETEMUL_RUNNER_SANDBOX_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <unordered_set>
#include "managed_environment.h"
#include "model/config.h"
#include "sandbox_env.h"

namespace netemul {

class SandboxArgs {
 public:
  std::string package;
  std::vector<std::string> args;
  // cmx facet override, used for testing.
  std::string cmx_facet_override;
};

class Sandbox {
 public:
  using TerminationReason = fuchsia::sys::TerminationReason;
  using TerminationCallback =
      fit::function<void(int64_t code, TerminationReason reason)>;
  using PackageLoadedCallback = fit::function<void()>;
  using RootEnvironmentCreatedCallback =
      fit::function<void(ManagedEnvironment*)>;
  explicit Sandbox(SandboxArgs args);

  void SetTerminationCallback(TerminationCallback callback) {
    termination_callback_ = std::move(callback);
  }

  void Start(async_dispatcher_t* dispatcher);

  void SetPackageLoadedCallback(PackageLoadedCallback callback) {
    package_loaded_callback_ = std::move(callback);
  }

  void SetRootEnvironmentCreatedCallback(
      RootEnvironmentCreatedCallback callback) {
    root_environment_created_callback_ = std::move(callback);
  }

  const SandboxEnv::Ptr& sandbox_environment() { return sandbox_env_; }

 private:
  void LoadPackage(fuchsia::sys::PackagePtr package);
  void Terminate(TerminationReason reason);
  void Terminate(int64_t exit_code, TerminationReason reason);
  void PostTerminate(TerminationReason reason);
  void PostTerminate(int64_t exit_code, TerminationReason reason);

  void EnableTestObservation();
  void RegisterTest(size_t ticket);
  void UnregisterTest(size_t ticket);

  template <typename T>
  bool LaunchProcess(fuchsia::sys::LauncherSyncPtr* launcher,
                     const std::string& url,
                     const std::vector<std::string>& arguments, bool is_test);

  bool LaunchSetup(fuchsia::sys::LauncherSyncPtr* launcher,
                   const std::string& url,
                   const std::vector<std::string>& arguments);

  bool CreateEnvironmentOptions(const config::Environment& config,
                                ManagedEnvironment::Options* options);
  bool ConfigureRootEnvironment();
  bool ConfigureEnvironment(
      fidl::SynchronousInterfacePtr<ManagedEnvironment::FManagedEnvironment>
          env,
      const config::Environment& config, bool root = false);
  bool ConfigureNetworks();
  bool LoadEnvironmentConfig(const rapidjson::Value& facet,
                             json::JSONParser* json_parser);

  async_dispatcher_t* main_dispatcher_;
  std::unique_ptr<async::Loop> helper_loop_;
  config::Config env_config_;
  bool setup_done_;
  SandboxArgs args_;
  SandboxEnv::Ptr sandbox_env_;
  TerminationCallback termination_callback_;
  PackageLoadedCallback package_loaded_callback_;
  RootEnvironmentCreatedCallback root_environment_created_callback_;
  fuchsia::sys::EnvironmentPtr parent_env_;
  fuchsia::sys::LoaderPtr loader_;
  ManagedEnvironment::Ptr root_;
  // keep network handles to keep objects alive
  std::vector<zx::channel> network_handles_;
  // keep component controller handles to keep processes alive
  std::vector<fuchsia::sys::ComponentControllerPtr> procs_;
  std::unordered_set<size_t> tests_;
};

}  // namespace netemul

#endif  // GARNET_BIN_NETEMUL_RUNNER_SANDBOX_H_
