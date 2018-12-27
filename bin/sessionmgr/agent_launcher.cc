// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/sessionmgr/agent_launcher.h"

#include <lib/fxl/logging.h>

namespace modular {
namespace {

constexpr char kEnvironmentLabel[] = "agent";

}  // namespace

component::Services AgentLauncher::StartAgent(
    const std::string& url,
    std::unique_ptr<maxwell::MaxwellServiceProviderBridge> bridge) {
  bridge_ = std::move(bridge);
  fuchsia::sys::ServiceListPtr service_list(new fuchsia::sys::ServiceList);
  service_list->names = bridge_->service_names();
  service_list->host_directory = bridge_->OpenAsDirectory();
  fuchsia::sys::EnvironmentPtr agent_env;
  environment_->CreateNestedEnvironment(
      agent_env.NewRequest(), /*controller=*/nullptr, kEnvironmentLabel,
      std::move(service_list), {});

  fuchsia::sys::LauncherPtr agent_launcher;
  agent_env->GetLauncher(agent_launcher.NewRequest());

  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = url;
  component::Services services;
  launch_info.directory_request = services.NewRequest();
  FXL_LOG(INFO) << "Starting modular agent " << url;
  agent_launcher->CreateComponent(std::move(launch_info), nullptr);
  return services;
}

}  // namespace modular
