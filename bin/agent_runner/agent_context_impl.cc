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

AgentContextImpl::AgentContextImpl(
    app::ApplicationLauncher* const app_launcher,
    MessageQueueManager* const message_queue_manager,
    AgentRunner* const agent_runner,
    ledger::LedgerRepository* ledger_repository,
    const std::string& url)
    : url_(url),
      agent_context_binding_(this),
      agent_runner_(agent_runner),
      component_context_impl_(
          {message_queue_manager, agent_runner, ledger_repository},
          url) {
  // Start up the agent process.
  auto launch_info = app::ApplicationLaunchInfo::New();
  launch_info->url = url;
  launch_info->services = application_services_.NewRequest();
  app_launcher->CreateApplication(std::move(launch_info),
                                  application_controller_.NewRequest());

  // Initialize the agent service.
  ConnectToService(application_services_.get(), agent_.NewRequest());
  agent_->Initialize(agent_context_binding_.NewBinding());

  // When the agent process dies, we remove it.
  application_controller_.set_connection_error_handler(
      [agent_runner, url] { agent_runner->RemoveAgent(url); });

  // When all the |AgentController| bindings go away maybe stop the agent.
  agent_controller_bindings_.set_on_empty_set_handler(
      [this] { MaybeStopAgent(); });
}

AgentContextImpl::~AgentContextImpl() = default;

void AgentContextImpl::NewConnection(
    const std::string& requestor_url,
    fidl::InterfaceRequest<app::ServiceProvider> incoming_services_request,
    fidl::InterfaceRequest<AgentController> agent_controller_request) {
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
    fidl::InterfaceRequest<ComponentContext> context) {
  component_context_bindings_.AddBinding(&component_context_impl_,
                                         std::move(context));
}

void AgentContextImpl::ScheduleTask(TaskInfoPtr task_info) {
  agent_runner_->ScheduleTask(url_, task_info->task_id,
                              task_info->trigger_condition->get_queue_name());
}

void AgentContextImpl::DeleteTask(const fidl::String& task_id) {
  agent_runner_->DeleteTask(url_, task_id);
}

void AgentContextImpl::Done() {}

void AgentContextImpl::MaybeStopAgent() {
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
