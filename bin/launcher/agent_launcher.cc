// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/launcher/agent_launcher.h"

namespace maxwell {
namespace {

constexpr char kEnvironmentLabel[] = "agent";

}  // namespace

void AgentLauncher::StartAgent(
    const std::string& url,
    std::unique_ptr<modular::ApplicationEnvironmentHost> env_host) {
  fidl::InterfaceHandle<modular::ApplicationEnvironmentHost> agent_host_handle =
      agent_host_bindings_.AddBinding(std::move(env_host));

  modular::ApplicationEnvironmentPtr agent_env;
  environment_->CreateNestedEnvironment(std::move(agent_host_handle),
                                        GetProxy(&agent_env), NULL,
                                        kEnvironmentLabel);

  modular::ApplicationLauncherPtr agent_launcher;
  agent_env->GetApplicationLauncher(GetProxy(&agent_launcher));

  auto launch_info = modular::ApplicationLaunchInfo::New();
  launch_info->url = url;
  agent_launcher->CreateApplication(std::move(launch_info), NULL);
}

}  // namespace maxwell
