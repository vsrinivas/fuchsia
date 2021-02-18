// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/internal/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fit/result.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/vfs/cpp/pseudo_dir.h>
#include <lib/vfs/cpp/pseudo_file.h>
#include <zircon/errors.h>

#include <iostream>
#include <regex>

#include <src/lib/files/glob.h>
#include <src/lib/fxl/command_line.h>
#include <src/modular/lib/modular_config/modular_config.h>
#include <src/modular/lib/modular_config/modular_config_constants.h>
#include <zxtest/zxtest.h>

constexpr char kBasemgrUrl[] = "fuchsia-pkg://fuchsia.com/basemgr#meta/basemgr.cmx";
constexpr char kBasemgrHubPath[] = "/hub/c/basemgr.cmx/*/out/debug/basemgr";
constexpr char kLaunchCommandString[] = "launch";
constexpr char kShutdownBasemgrCommandString[] = "shutdown";
constexpr char kClearConfigCommandString[] = "delete_config";
constexpr char kDisableRestartAgentOnCrashFlagString[] = "disable_agent_restart_on_crash";

// Returns false if no basemgr debug service can be found, because
// basemgr is not running.
bool FindBasemgrDebugService(std::string* service_path) {
  glob_t globbuf;
  glob(kBasemgrHubPath, 0, nullptr, &globbuf);
  bool found = false;
  if (globbuf.gl_pathc > 0) {
    *service_path = globbuf.gl_pathv[0];
    found = true;
  }
  globfree(&globbuf);
  return found;
}

zx_status_t ShutdownBasemgr(async::Loop* loop) {
  // Get a connection to basemgr in order to shut it down.
  std::string service_path;
  if (!FindBasemgrDebugService(&service_path)) {
    // basemgr is not running.
    std::cerr << "basemgr does not appear to be running.";
    return ZX_OK;
  }
  fuchsia::modular::internal::BasemgrDebugPtr basemgr_debug;
  auto request = basemgr_debug.NewRequest().TakeChannel();
  auto service_connect_result = fdio_service_connect(service_path.c_str(), request.get());
  if (service_connect_result != ZX_OK) {
    std::cerr << "Could not connect to basemgr service at " << service_path << ": "
              << zx_status_get_string(service_connect_result);
    return service_connect_result;
  }

  basemgr_debug->Shutdown();
  // Wait for basemgr to shutdown.
  zx_status_t channel_close_status;
  basemgr_debug.set_error_handler([&channel_close_status, loop](zx_status_t status) {
    channel_close_status = status;
    loop->Quit();
  });
  loop->Run();
  loop->ResetQuit();
  if (channel_close_status != ZX_OK) {
    std::cerr << "basemgr did not tear down cleanly. Expected ZX_OK, got "
              << zx_status_get_string(channel_close_status);
  }
  return ZX_OK;
}

zx_status_t ClearPersistedConfig(async::Loop* loop) {
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = kBasemgrUrl;
  launch_info.arguments = {"delete_persistent_config"};

  std::unique_ptr<sys::ComponentContext> context =
      sys::ComponentContext::CreateAndServeOutgoingDirectory();
  fuchsia::sys::LauncherPtr launcher;
  fuchsia::sys::ComponentControllerPtr controller;
  context->svc()->Connect(launcher.NewRequest());
  launcher->CreateComponent(std::move(launch_info), controller.NewRequest());

  zx_status_t channel_close_status;
  controller.set_error_handler([&channel_close_status, loop](zx_status_t status) {
    channel_close_status = status;
    loop->Quit();
  });
  loop->Run();
  loop->ResetQuit();
  if (channel_close_status != ZX_OK) {
    std::cerr << "basemgr delete_peristent_config did not exit cleanly. Expected ZX_OK, got "
              << zx_status_get_string(channel_close_status);
  }
  return ZX_OK;
}

std::unique_ptr<vfs::PseudoDir> CreateConfigPseudoDir(std::string config_str) {
  auto dir = std::make_unique<vfs::PseudoDir>();
  dir->AddEntry(modular_config::kStartupConfigFilePath,
                std::make_unique<vfs::PseudoFile>(
                    config_str.length(), [config_str = std::move(config_str)](
                                             std::vector<uint8_t>* out, size_t /*unused*/) {
                      std::copy(config_str.begin(), config_str.end(), std::back_inserter(*out));
                      return ZX_OK;
                    }));
  return dir;
}

zx_status_t LaunchBasemgr(async::Loop* loop, bool disable_agent_restart_on_crash) {
  // If basemgr is already running, shut it down first.
  if (files::Glob(kBasemgrHubPath).size() != 0) {
    auto result = ShutdownBasemgr(loop);
    if (result != ZX_OK) {
      std::cerr << "Could not shut down running instance of basemgr: "
                << zx_status_get_string(result);
      return result;
    }
  }

  // Read the configuration file in from stdin.
  std::string config_str;
  std::string line;
  while (getline(std::cin, line)) {
    config_str += line;
  }

  auto config = modular::DefaultConfig();
  if (!config_str.empty()) {
    auto parse_result = modular::ParseConfig(config_str);
    if (parse_result.is_error()) {
      std::cerr << "Could not parse ModularConfig: " << parse_result.error();
      return ZX_ERR_INVALID_ARGS;
    }
    config = parse_result.take_ok_result().value;
  }

  if (disable_agent_restart_on_crash) {
    config.mutable_sessionmgr_config()->set_disable_agent_restart_on_crash(true);
  }

  // Create the pseudo directory with our config "file" mapped to
  // kConfigFilename.
  auto config_dir = CreateConfigPseudoDir(modular::ConfigToJsonString(config));
  fidl::InterfaceHandle<fuchsia::io::Directory> dir_handle;
  config_dir->Serve(fuchsia::io::OPEN_RIGHT_READABLE, dir_handle.NewRequest().TakeChannel());

  // Build a LaunchInfo with the config directory above mapped to
  // /config_override/data.
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = kBasemgrUrl;
  launch_info.flat_namespace = fuchsia::sys::FlatNamespace::New();
  launch_info.flat_namespace->paths.push_back(modular_config::kOverriddenConfigDir);
  launch_info.flat_namespace->directories.push_back(dir_handle.TakeChannel());

  // Quit the loop when basemgr's out directory has been mounted.
  fuchsia::sys::ComponentControllerPtr controller;
  controller.events().OnDirectoryReady = [&controller, &loop] {
    controller->Detach();
    loop->Quit();
  };

  // Launch a basemgr instance with the custom namespace we created above.
  std::unique_ptr<sys::ComponentContext> context =
      sys::ComponentContext::CreateAndServeOutgoingDirectory();
  fuchsia::sys::LauncherPtr launcher;
  context->svc()->Connect(launcher.NewRequest());
  launcher->CreateComponent(std::move(launch_info), controller.NewRequest());

  loop->Run();
  return ZX_OK;
}

std::string GetUsage() {
  return R"(Control the lifecycle of instances of basemgr.

Usage: basemgr_launcher [<command>] [--disable_agent_restart_on_crash] 

  <command>
    (none)         Alias for 'launch'.
    launch         Launches a new instance of basemgr with a modular JSON configuration
                   read from stdin.
    shutdown       Terminates the running instance of basemgr, if found.
    delete_config  Clears any cached persistent configuration (see below).
    
  --disable_agent_restart_on_crash

    Convenience flag for the 'launch' command to set
    ModularConfig.sessionmgr_config.disable_agent_restart_on_crash to true.
    Equivalent to setting the flag to true in the ModularConfig provided as
    input to the 'launch' command.

# Examples (from host machine):

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

int main(int argc, const char** argv) {
  syslog::SetTags({"basemgr_launcher"});
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  const auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  const auto& positional_args = command_line.positional_args();

  const auto& cmd = positional_args.empty() ? kLaunchCommandString : positional_args[0];

  if (cmd == kShutdownBasemgrCommandString) {
    return ShutdownBasemgr(&loop);
  }
  if (cmd == kClearConfigCommandString) {
    return ClearPersistedConfig(&loop);
  }
  if (cmd == kLaunchCommandString) {
    return LaunchBasemgr(&loop, command_line.HasOption(kDisableRestartAgentOnCrashFlagString));
  }

  std::cerr << GetUsage() << std::endl;
  return ZX_ERR_INVALID_ARGS;
}
