// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/maxwell/agent_launcher.h"

#include <lib/fxl/logging.h>

namespace maxwell {
namespace {

constexpr char kEnvironmentLabel[] = "agent";

}  // namespace

component::Services AgentLauncher::StartAgent(
    const std::string& url,
    std::unique_ptr<MaxwellServiceProviderBridge> bridge) {
  bridge_ = std::move(bridge);
  fuchsia::sys::EnvironmentPtr agent_env;
  environment_->CreateNestedEnvironment(bridge_->OpenAsDirectory(),
                                        agent_env.NewRequest(), NULL,
                                        kEnvironmentLabel);

  fuchsia::sys::LauncherPtr agent_launcher;
  agent_env->GetLauncher(agent_launcher.NewRequest());

  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = url;
  component::Services services;
  launch_info.directory_request = services.NewRequest();
  FXL_LOG(INFO) << "Starting Maxwell agent " << url;
  agent_launcher->CreateComponent(std::move(launch_info), NULL);
  return services;
}

}  // namespace maxwell
