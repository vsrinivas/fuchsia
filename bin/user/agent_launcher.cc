// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user/agent_launcher.h"

namespace maxwell {
namespace {

constexpr char kEnvironmentLabel[] = "agent";

}  // namespace

void AgentLauncher::StartAgent(
    const std::string& url,
    std::unique_ptr<app::ApplicationEnvironmentHost> env_host) {
  fidl::InterfaceHandle<app::ApplicationEnvironmentHost> agent_host_handle =
      agent_host_bindings_.AddBinding(std::move(env_host));

  app::ApplicationEnvironmentPtr agent_env;
  environment_->CreateNestedEnvironment(std::move(agent_host_handle),
                                        agent_env.NewRequest(), NULL,
                                        kEnvironmentLabel);

  app::ApplicationLauncherPtr agent_launcher;
  agent_env->GetApplicationLauncher(agent_launcher.NewRequest());

  auto launch_info = app::ApplicationLaunchInfo::New();
  launch_info->url = url;
  FXL_LOG(INFO) << "Starting Maxwell agent " << url;
  agent_launcher->CreateApplication(std::move(launch_info), NULL);
}

}  // namespace maxwell
