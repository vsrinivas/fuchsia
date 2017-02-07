// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/agent_runner/agent_context_impl.h"

#include "apps/modular/lib/app/connect.h"
#include "apps/modular/src/agent_runner/agent_runner.h"

namespace modular {

AgentContextImpl::AgentContextImpl(ApplicationLauncher* app_launcher,
                                   AgentRunner* agent_runner,
                                   const std::string& url)
    : url_(url), agent_context_(this) {
  // Start up the agent process.
  auto launch_info = ApplicationLaunchInfo::New();
  launch_info->url = url;
  launch_info->services = application_services_.NewRequest();
  app_launcher->CreateApplication(std::move(launch_info),
                                  application_controller_.NewRequest());

  // Initialize the agent service.
  ConnectToService(application_services_.get(), agent_.NewRequest());
  agent_->Initialize(agent_context_.NewBinding());

  // When the agent process dies, we remove it.
  application_controller_.set_connection_error_handler(
      [agent_runner, url] { agent_runner->RemoveAgent(url); });
}

AgentContextImpl::~AgentContextImpl() = default;

void AgentContextImpl::NewConnection(
    const std::string& requestor_url,
    fidl::InterfaceRequest<modular::ServiceProvider> incoming_services,
    fidl::InterfaceRequest<modular::AgentController> controller) {
  agent_->Connect(requestor_url, std::move(incoming_services));

  // TODO: |controller| dies here. Implement proper refcounting.
}

void AgentContextImpl::GetComponentContext(
    fidl::InterfaceRequest<modular::ComponentContext> context) {}

void AgentContextImpl::ScheduleTask(TaskInfoPtr task_info) {}

void AgentContextImpl::DeleteTask(const fidl::String& task_id) {}

void AgentContextImpl::Done() {}

}  // namespace modular
