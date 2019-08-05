// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>
#include <string>
#include <vector>

#include <fuchsia/io/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/vfs/cpp/pseudo_dir.h>
#include <lib/vfs/cpp/pseudo_file.h>
#include <peridot/lib/modular_config/modular_config.h>
#include <peridot/lib/modular_config/modular_config_constants.h>
#include <src/lib/files/glob.h>
#include <src/lib/fxl/command_line.h>


constexpr char kBasemgrUrl[] = "fuchsia-pkg://fuchsia.com/basemgr#meta/basemgr.cmx";
constexpr char kBasemgrHubGlob[] = "/hub/c/basemgr.cmx/*";
constexpr char kGetUsage[] = "GetUsage";
constexpr char kSessionAgent[] = "session_agent";

std::unique_ptr<vfs::PseudoDir> CreateConfigPseudoDir(std::string config_str) {
  // Read the configuration file in from stdin.
  if (config_str.empty()) {
    std::string line;
    while (getline(std::cin, line)) {
      config_str += line;
    }
  }

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

std::string GetUsage() {
  return R"(A thin wrapper that takes a config file from stdin and maps it to
/config_override/data/startup.config for a new basemgr instance.

  Usage:

cat myconfig.json | fx shell basemgr_launcher)";
}

std::string GetConfigFromArgs(fxl::CommandLine command_line) {
  auto config_reader = modular::ModularConfigReader(/*config=*/"{}", /*config_path=*/"");
  auto basemgr_config = config_reader.GetBasemgrConfig();
  auto sessionmgr_config = config_reader.GetSessionmgrConfig();

  for (auto opt : command_line.options()) {
    if (opt.name == modular_config::kBaseShell) {
      basemgr_config.mutable_base_shell()->mutable_app_config()->set_url(opt.value);
    } else if (opt.name == modular_config::kSessionShell) {
      basemgr_config.mutable_session_shell_map()
          ->at(0)
          .mutable_config()
          ->mutable_app_config()
          ->set_url(opt.value);
    } else if (opt.name == kSessionAgent) {
      sessionmgr_config.mutable_session_agents()->push_back(opt.value);
    } else {
      return kGetUsage;
    }
  }

  return modular::ModularConfigReader::GetConfigAsString(&basemgr_config, &sessionmgr_config);
}

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  // Check if basemgr already exists, if so suggest killing it.
  bool exists = files::Glob(kBasemgrHubGlob).size() != 0;
  if (exists) {
    std::cerr << "basemgr is already running!" << std::endl
              << "To kill: `fx shell killall basemgr.cmx`" << std::endl;
    return 1;
  }

  std::string config_str = "";
  if (argc > 1) {
    const auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
    config_str = GetConfigFromArgs(std::move(command_line));
    if (config_str == kGetUsage) {
      std::cout << GetUsage() << std::endl;
      return 1;
    }
  }

  // Create the pseudo directory with our config "file" mapped to
  // kConfigFilename.
  auto config_dir = CreateConfigPseudoDir(config_str);
  fidl::InterfaceHandle<fuchsia::io::Directory> dir_handle;
  config_dir->Serve(fuchsia::io::OPEN_RIGHT_READABLE, dir_handle.NewRequest().TakeChannel());

  // Build a LaunchInfo with the config directory above mapped to
  // /config_override/data.
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = kBasemgrUrl;
  launch_info.flat_namespace = fuchsia::sys::FlatNamespace::New();
  launch_info.flat_namespace->paths.push_back(modular_config::kOverriddenConfigDir);
  launch_info.flat_namespace->directories.push_back(dir_handle.TakeChannel());

  // Launch a basemgr instance with the custom namespace we created above.
  std::unique_ptr<sys::ComponentContext> context = sys::ComponentContext::Create();
  fuchsia::sys::LauncherPtr launcher;
  context->svc()->Connect(launcher.NewRequest());
  fidl::InterfacePtr<fuchsia::sys::ComponentController> controller;
  launcher->CreateComponent(std::move(launch_info), controller.NewRequest());

  async::PostDelayedTask(
      loop.dispatcher(),
      [&controller, &loop] {
        controller->Detach();
        loop.Quit();
      },
      zx::sec(5));

  loop.Run();
  return 0;
}
