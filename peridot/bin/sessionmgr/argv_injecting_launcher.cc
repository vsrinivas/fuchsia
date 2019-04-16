// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/sessionmgr/argv_injecting_launcher.h"

namespace modular {

ArgvInjectingLauncher::ArgvInjectingLauncher(
    fuchsia::sys::LauncherPtr parent_launcher, ArgvMap per_component_argv)
    : parent_launcher_(std::move(parent_launcher)),
      per_component_argv_(std::move(per_component_argv)) {}

ArgvInjectingLauncher::~ArgvInjectingLauncher() = default;

void ArgvInjectingLauncher::CreateComponent(
    fuchsia::sys::LaunchInfo launch_info,
    fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller) {
  auto it = per_component_argv_.find(launch_info.url);
  if (it != per_component_argv_.end()) {
    launch_info.arguments.reset(it->second);
  }

  parent_launcher_->CreateComponent(std::move(launch_info),
                                    std::move(controller));
}

}  // namespace modular
