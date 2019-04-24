// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>
#include <string>
#include <vector>

#include <fuchsia/io/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/vfs/cpp/pseudo_dir.h>
#include <lib/vfs/cpp/pseudo_file.h>
#include <src/lib/files/glob.h>
#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/strings/string_printf.h>

constexpr char kConfigFilename[] = "startup.config";
constexpr char kBasemgrUrl[] =
    "fuchsia-pkg://fuchsia.com/basemgr#meta/basemgr.cmx";
constexpr char kBasemgrHubGlob[] = "/hub/c/basemgr.cmx/*";

std::unique_ptr<vfs::PseudoDir> CreateConfigPseudoDir() {
  // Read the configuration file in from stdin.
  std::string config_str;
  std::string line;
  while (getline(std::cin, line)) {
    config_str += line;
  }

  auto dir = std::make_unique<vfs::PseudoDir>();
  dir->AddEntry(
      kConfigFilename,
      std::make_unique<vfs::PseudoFile>(
          [config_str = std::move(config_str)](std::vector<uint8_t>* out) {
            std::copy(config_str.begin(), config_str.end(),
                      std::back_inserter(*out));
            return ZX_OK;
          }));
  return dir;
}

std::string GetUsage() {
  return R"(A thin wrapper that takes a config file from stdin and maps it to
/config_override/data/startup.config for a new basemgr instance.

  Usage:

cat myconfig.json | fx shell basemgr_launcher &)";
}

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  if (argc > 1) {
    std::cout << GetUsage() << std::endl;
    return 1;
  }

  // Check if basemgr already exists, if so suggest killing it.
  bool exists = files::Glob(kBasemgrHubGlob).size() != 0;
  if (exists) {
    std::cerr << "basemgr is already running!" << std::endl
              << "To kill: `fx shell killall basemgr.cmx && fx shell killall "
                 "basemgr_launcher`";
    return 1;
  }

  // Create the pseudo directory with our config "file" mapped to
  // kConfigFilename.
  auto config_dir = CreateConfigPseudoDir();
  fidl::InterfaceHandle<fuchsia::io::Directory> dir_handle;
  config_dir->Serve(fuchsia::io::OPEN_RIGHT_READABLE,
                    dir_handle.NewRequest().TakeChannel());

  // Build a LaunchInfo with the config directory above mapped to
  // /config_override/data.
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = kBasemgrUrl;
  launch_info.flat_namespace = fuchsia::sys::FlatNamespace::New();
  launch_info.flat_namespace->paths.push_back("/config_override/data");
  launch_info.flat_namespace->directories.push_back(dir_handle.TakeChannel());

  // Launch a basemgr instance with the custom namespace we created above.
  std::unique_ptr<component::StartupContext> context =
      component::StartupContext::CreateFromStartupInfo();
  fuchsia::sys::LauncherPtr launcher;
  context->ConnectToEnvironmentService(launcher.NewRequest());
  fidl::InterfaceHandle<fuchsia::sys::ComponentController> controller;
  launcher->CreateComponent(std::move(launch_info), controller.NewRequest());

  loop.Run();
  return 0;
}