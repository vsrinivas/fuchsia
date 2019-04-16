// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_SANDBOX_H_
#define SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_SANDBOX_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async_promise/executor.h>
#include <lib/fit/promise.h>
#include <src/connectivity/network/testing/netemul/lib/network/netdump.h>

#include <unordered_set>

#include "managed_environment.h"
#include "model/config.h"
#include "sandbox_env.h"

namespace netemul {

class SandboxArgs {
 public:
  bool ParseFromCmxFileAt(int dir, const std::string& path);
  bool ParseFromString(const std::string& config);
  bool ParseFromJSON(const rapidjson::Value& facet,
                     json::JSONParser* json_parser);

  config::Config config;
};

class Sandbox {
 public:
  using TerminationReason = fuchsia::sys::TerminationReason;
  using TerminationCallback =
      fit::function<void(int64_t code, TerminationReason reason)>;
  using ServicesCreatedCallback = fit::function<void()>;
  using RootEnvironmentCreatedCallback =
      fit::function<void(ManagedEnvironment*)>;
  explicit Sandbox(SandboxArgs args);

  void SetTerminationCallback(TerminationCallback callback) {
    termination_callback_ = std::move(callback);
  }

  void Start(async_dispatcher_t* dispatcher);

  void SetServicesCreatedCallback(ServicesCreatedCallback callback) {
    services_created_callback_ = std::move(callback);
  }

  void SetRootEnvironmentCreatedCallback(
      RootEnvironmentCreatedCallback callback) {
    root_environment_created_callback_ = std::move(callback);
  }

  const SandboxEnv::Ptr& sandbox_environment() { return sandbox_env_; }

 private:
  using ConfiguringEnvironmentPtr = std::shared_ptr<
      fidl::SynchronousInterfacePtr<ManagedEnvironment::FManagedEnvironment>>;
  using ConfiguringEnvironmentLauncher =
      std::shared_ptr<fidl::SynchronousInterfacePtr<fuchsia::sys::Launcher>>;

  void StartEnvironments();
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

  fit::promise<> LaunchSetup(fuchsia::sys::LauncherSyncPtr* launcher,
                             const std::string& url,
                             const std::vector<std::string>& arguments);

  fit::promise<> StartChildEnvironment(ConfiguringEnvironmentPtr parent,
                                       const config::Environment* config);
  fit::promise<> StartEnvironmentInner(ConfiguringEnvironmentPtr environment,
                                       const config::Environment* config);
  fit::promise<> StartEnvironmentSetup(const config::Environment* config,
                                       ConfiguringEnvironmentLauncher launcher);
  fit::promise<> StartEnvironmentAppsAndTests(
      const config::Environment* config,
      ConfiguringEnvironmentLauncher launcher);

  bool CreateEnvironmentOptions(const config::Environment& config,
                                ManagedEnvironment::Options* options);
  void ConfigureRootEnvironment();
  fit::promise<> ConfigureEnvironment(ConfiguringEnvironmentPtr env,
                                      const config::Environment* config,
                                      bool root = false);
  bool ConfigureNetworks();

  async_dispatcher_t* main_dispatcher_;
  std::unique_ptr<async::Loop> helper_loop_;
  std::unique_ptr<async::Executor> helper_executor_;
  config::Config env_config_;
  bool setup_done_;
  bool test_spawned_;
  SandboxEnv::Ptr sandbox_env_;
  TerminationCallback termination_callback_;
  ServicesCreatedCallback services_created_callback_;
  RootEnvironmentCreatedCallback root_environment_created_callback_;
  fuchsia::sys::EnvironmentPtr parent_env_;
  fuchsia::sys::LoaderPtr loader_;
  ManagedEnvironment::Ptr root_;
  // keep network handles to keep objects alive
  std::vector<zx::channel> network_handles_;
  // keep component controller handles to keep processes alive
  std::vector<fuchsia::sys::ComponentControllerPtr> procs_;
  std::unordered_set<size_t> tests_;
  std::unique_ptr<NetWatcher<InMemoryDump>> net_dumps_;
};

}  // namespace netemul

#endif  // SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_SANDBOX_H_
