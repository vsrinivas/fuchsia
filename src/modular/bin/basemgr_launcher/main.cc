// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/executor.h>
#include <lib/fpromise/result.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/log_settings.h>
#include <zircon/errors.h>

#include <iostream>
#include <optional>

#include "src/lib/fxl/command_line.h"
#include "src/modular/lib/modular_config/modular_config.h"
#include "src/modular/lib/modular_config/modular_config_constants.h"
#include "src/modular/lib/session/session.h"
#include "zircon/status.h"

constexpr char kLaunchCommandString[] = "launch";
constexpr char kShutdownBasemgrCommandString[] = "shutdown";
constexpr char kDeleteConfigCommandString[] = "delete_config";
constexpr char kDisableRestartAgentOnCrashFlagString[] = "disable_agent_restart_on_crash";

// Reads and parses a |ModularConfig| from stdin.
fpromise::result<fuchsia::modular::session::ModularConfig, zx_status_t> ReadConfig() {
  // Read the configuration in from stdin.
  std::string config_str;
  std::string line;
  while (getline(std::cin, line)) {
    config_str += line;
  }

  if (config_str.empty()) {
    return fpromise::ok(modular::DefaultConfig());
  }

  auto parse_result = modular::ParseConfig(config_str);
  if (parse_result.is_error()) {
    std::cerr << "Could not parse ModularConfig: " << parse_result.error();
    return fpromise::error(ZX_ERR_INVALID_ARGS);
  }
  return parse_result.take_ok_result();
}

std::string GetUsage() {
  return R"(Control the lifecycle of instances of basemgr.

Usage: basemgr_launcher [<command>] [<flag>...]

  <command>
    (none)         Alias for 'launch'.
    launch         Launches a new instance of basemgr with a modular JSON configuration
                   read from stdin.
    shutdown       Terminates the running instance of basemgr, if found.
    delete_config  Clears any cached persistent configuration (see below).

# Flags

launch:

  --disable_agent_restart_on_crash

    Sets ModularConfig.sessionmgr_config.disable_agent_restart_on_crash to true.
    Equivalent to setting the flag to true in the ModularConfig provided in stdin.

# Examples (from host machine)

  $ cat myconfig.json | fx shell basemgr_launcher
  $ fx shell basemgr_launcher shutdown

# Persistent configuration

Persistent configuration can enabled by adding //src/modular/build:allow_persistent_config_override
to a non-production build. When enabled, the configuration provided to basemgr_launcher will
be stored and used when basemgr restarts and across reboots.

This configuration can be deleted by running (from host machine)

  $ fx shell basemgr_launcher delete_config
)";
}

// Returns result's error value, or ZX_OK if result is OK.
zx_status_t ToStatus(fpromise::result<void, zx_status_t> result) {
  if (result.is_error()) {
    return result.error();
  }
  return ZX_OK;
}

// Runs |promise| to completion on |loop| and returns the value.
template <typename PromiseType>
typename PromiseType::result_type RunPromise(async::Loop* loop, PromiseType promise) {
  async::Executor executor{loop->dispatcher()};
  std::optional<typename PromiseType::result_type> res;
  executor.schedule_task(
      promise.then([&res](typename PromiseType::result_type& v) { res = std::move(v); }));

  while (loop->GetState() == ASYNC_LOOP_RUNNABLE) {
    if (res.has_value()) {
      loop->ResetQuit();
      break;
    }
    loop->Run(zx::deadline_after(zx::duration::infinite()), true);
  }

  return std::move(res.value());
}

int main(int argc, const char** argv) {
  syslog::SetTags({"basemgr_launcher"});
  async::Loop loop{&kAsyncLoopConfigAttachToCurrentThread};

  const auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  const auto& positional_args = command_line.positional_args();

  const auto& cmd = positional_args.empty() ? kLaunchCommandString : positional_args[0];

  // Connect to fuchsia.sys.Launcher that is used to launch basemgr as a v1 component.
  auto context = sys::ComponentContext::Create();
  fuchsia::sys::LauncherPtr launcher;
  context->svc()->Connect(launcher.NewRequest());

  if (cmd == kShutdownBasemgrCommandString) {
    return ToStatus(RunPromise(&loop, modular::session::MaybeShutdownBasemgr()));
  }
  if (cmd == kDeleteConfigCommandString) {
    return ToStatus(RunPromise(&loop, modular::session::DeletePersistentConfig(launcher.get())));
  }
  if (cmd == kLaunchCommandString) {
    auto config_result = ReadConfig();
    if (config_result.is_error()) {
      return config_result.take_error();
    }

    auto config = config_result.take_value();
    if (command_line.HasOption(kDisableRestartAgentOnCrashFlagString)) {
      config.mutable_sessionmgr_config()->set_disable_agent_restart_on_crash(true);
    }

    return ToStatus(RunPromise(&loop, modular::session::Launch(launcher.get(), std::move(config))));
  }

  std::cerr << GetUsage() << std::endl;
  return ZX_ERR_INVALID_ARGS;
}
