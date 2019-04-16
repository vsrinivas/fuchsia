// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SESSIONMGR_ARGV_INJECTING_LAUNCHER_H_
#define PERIDOT_BIN_SESSIONMGR_ARGV_INJECTING_LAUNCHER_H_

#include <fuchsia/sys/cpp/fidl.h>

#include <map>
#include <string>

namespace modular {

// A fuchsia.sys.Launcher which optionally overrides LaunchInfo.arguments for
// components which have an entry for their LaunchInfo.url in
// |per_component_argv|.
class ArgvInjectingLauncher : public fuchsia::sys::Launcher {
 public:
  // A map from Component URI -> vector of argv.
  using ArgvMap = std::map<std::string, std::vector<std::string>>;

  ArgvInjectingLauncher(fuchsia::sys::LauncherPtr parent_launcher,
                        ArgvMap per_component_argv);
  ~ArgvInjectingLauncher() override;

  // fuchsia::sys::Launcher
  void CreateComponent(fuchsia::sys::LaunchInfo launch_info,
                       fidl::InterfaceRequest<fuchsia::sys::ComponentController>
                           controller) override;

 private:
  fuchsia::sys::LauncherPtr parent_launcher_;
  ArgvMap per_component_argv_;
};

}  // namespace modular

#endif  // PERIDOT_BIN_SESSIONMGR_ARGV_INJECTING_LAUNCHER_H_
