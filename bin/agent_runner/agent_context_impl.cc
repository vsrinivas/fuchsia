// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/agent_runner/agent_context_impl.h"

#include "application/lib/app/connect.h"
#include "apps/modular/src/agent_runner/agent_runner.h"
#include "lib/mtl/tasks/message_loop.h"

namespace modular {

namespace {

constexpr ftl::TimeDelta kKillTimeout = ftl::TimeDelta::FromSeconds(2);

}  // namespace

AgentContextImpl::AgentContextImpl(const AgentContextInfo& info,
                                   const std::string& url)
    : url_(url),
      agent_context_binding_(this),
      agent_runner_(info.component_context_info.agent_runner),
      component_context_impl_(info.component_context_info, url),
      user_intelligence_provider_(info.user_intelligence_provider) {
  // Start up the agent process.
  auto launch_info = app::ApplicationLaunchInfo::New();
  launch_info->url = url;
  launch_info->services = application_services_.NewRequest();
  info.app_launcher->CreateApplication(std::move(launch_info),
                                       application_controller_.NewRequest());

  // Initialize the agent service.
  ConnectToService(application_services_.get(), agent_.NewRequest());
  agent_->Initialize(agent_context_binding_.NewBinding(),
                     [this] { OnInitialized(); });

  // When the agent process dies, we remove it.
  application_controller_.set_connection_error_handler(
      [this] { agent_runner_->RemoveAgent(url_); });

  // When all the |AgentController| bindings go away maybe stop the agent.
  agent_controller_bindings_.set_on_empty_set_handler(
      [this] { MaybeStopAgent(); });
}

AgentContextImpl::~AgentContextImpl() = default;

void AgentContextImpl::OnInitialized() {
  ready_ = true;
  for (auto& pending_connection : pending_connections_) {
    NewConnection(pending_connection.requestor_url,
                  std::move(pending_connection.incoming_services_request),
                  std::move(pending_connection.agent_controller_request));
  }
  pending_connections_.clear();
}

void AgentContextImpl::NewConnection(
    const std::string& requestor_url,
    fidl::InterfaceRequest<app::ServiceProvider> incoming_services_request,
    fidl::InterfaceRequest<AgentController> agent_controller_request) {
  if (!ready_) {
    pending_connections_.push_back({requestor_url,
                                    std::move(incoming_services_request),
                                    std::move(agent_controller_request)});
    return;
  }

  agent_->Connect(requestor_url, std::move(incoming_services_request));

  // Add a binding to the |controller|. When all the bindings go away
  // we can stop the agent.
  agent_controller_bindings_.AddBinding(this,
                                        std::move(agent_controller_request));
}

void AgentContextImpl::NewTask(const std::string& task_id) {
  // Increment the counter for number of incomplete tasks. Decrement it when we
  // receive its callback;
  incomplete_task_count_++;
  agent_->RunTask(task_id, [this] {
    incomplete_task_count_--;
    MaybeStopAgent();
  });
}

void AgentContextImpl::GetComponentContext(
    fidl::InterfaceRequest<ComponentContext> request) {
  component_context_bindings_.AddBinding(&component_context_impl_,
                                         std::move(request));
}

void AgentContextImpl::GetIntelligenceServices(
    fidl::InterfaceRequest<maxwell::IntelligenceServices> request) {
  user_intelligence_provider_->GetComponentIntelligenceServices(
      nullptr, url_, std::move(request));
}

void AgentContextImpl::ScheduleTask(TaskInfoPtr task_info) {
  agent_runner_->ScheduleTask(url_, std::move(task_info));
}

void AgentContextImpl::DeleteTask(const fidl::String& task_id) {
  agent_runner_->DeleteTask(url_, task_id);
}

void AgentContextImpl::Done() {}

void AgentContextImpl::MaybeStopAgent() {
  // TODO(mesch): The code to stop modules does the same but uses
  // different primitives.
  if (agent_controller_bindings_.size() == 0 && incomplete_task_count_ == 0) {
    auto kill_agent_once = std::make_shared<std::once_flag>();
    auto kill_agent = [kill_agent_once, this]() mutable {
      std::call_once(*kill_agent_once.get(), [this] {
        // TODO(alhaad): This is not enough. We need to close and drain the
        // AgentContext binding.
        agent_runner_->RemoveAgent(url_);
      });
    };
    agent_->Stop(kill_agent);
    kill_timer_.Start(mtl::MessageLoop::GetCurrent()->task_runner().get(),
                      kill_agent, kKillTimeout);
  }
}

}  // namespace modular
