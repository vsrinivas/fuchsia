// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/agent_runner/agent_context_impl.h"

#include "application/lib/app/connect.h"
#include "apps/modular/src/agent_runner/agent_runner.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/mtl/tasks/message_loop.h"

namespace modular {

namespace {

constexpr ftl::TimeDelta kKillTimeout = ftl::TimeDelta::FromSeconds(2);
}

class AgentContextImpl::InitializeCall : Operation<void> {
 public:
  InitializeCall(OperationContainer* const container,
                 bool* const ready,
                 Agent* const agent,
                 fidl::Binding<AgentContext>* const agent_context_binding,
                 ResultCall result_call)
      : Operation(container, std::move(result_call)),
        ready_(ready),
        agent_context_binding_(agent_context_binding),
        agent_(agent) {
    Ready();
  }

 private:
  void Run() override {
    if (*ready_) {
      // Means that the agent is already initialized.
      Done();
      return;
    }

    // TODO(alhaad): We should have a timer for an agent which does not return
    // its callback within some timeout.
    agent_->Initialize(agent_context_binding_->NewBinding(), [this] {
      *ready_ = true;
      Done();
    });
  }

  bool* const ready_;
  fidl::Binding<AgentContext>* const agent_context_binding_;
  Agent* const agent_;

  FTL_DISALLOW_COPY_AND_ASSIGN(InitializeCall);
};

// If |is_terminating| is set to true, the agent will be torn down irrespective
// of whether there is an open-connection or running task.
class AgentContextImpl::StopCall : Operation<void> {
 public:
  StopCall(OperationContainer* const container,
           bool* const ready,
           bool terminating,
           int* const incomplete_task_count,
           fidl::BindingSet<AgentController>* const agent_controller_bindings,
           Agent* const agent,
           const std::function<void()>& reset_agent_connection,
           ResultCall result_call)
      : Operation(container, std::move(result_call)),
        ready_(ready),
        terminating_(terminating),
        incomplete_task_count_(incomplete_task_count),
        agent_controller_bindings_(agent_controller_bindings),
        agent_(agent),
        reset_agent_connection_(reset_agent_connection) {
    Ready();
  }

 private:
  void Run() override {
    if (!(*ready_)) {
      // Means that the agent is already stopped.
      Done();
      return;
    }

    if (terminating_ || (agent_controller_bindings_->size() == 0 &&
                         *incomplete_task_count_ == 0)) {
      Stop();
      return;
    }

    Done();
  }

  void Stop() {
    auto kill_agent_once = std::make_shared<std::once_flag>();
    auto kill_agent = [kill_agent_once, this]() mutable {
      std::call_once(*kill_agent_once.get(), [this] {
        reset_agent_connection_();
        Done();
      });
    };
    agent_->Stop(kill_agent);
    kill_timer_.Start(mtl::MessageLoop::GetCurrent()->task_runner().get(),
                      kill_agent, kKillTimeout);
  }

  bool* const ready_;
  const bool terminating_;
  int* const incomplete_task_count_;
  fidl::BindingSet<AgentController>* const agent_controller_bindings_;
  Agent* const agent_;
  const std::function<void()> reset_agent_connection_;

  ftl::OneShotTimer kill_timer_;

  FTL_DISALLOW_COPY_AND_ASSIGN(StopCall);
};

AgentContextImpl::AgentContextImpl(const AgentContextInfo& info,
                                   const std::string& url)
    : url_(url),
      application_launcher_(info.app_launcher),
      agent_context_binding_(this),
      agent_runner_(info.component_context_info.agent_runner),
      component_context_impl_(info.component_context_info,
                              kAgentComponentNamespace,
                              url),
      token_provider_factory_(info.token_provider_factory),
      user_intelligence_provider_(info.user_intelligence_provider) {}

AgentContextImpl::~AgentContextImpl() = default;

void AgentContextImpl::StopForTeardown(const std::function<void()>& callback) {
  new StopCall(&operation_queue_, &ready_, true /* is_terminating */,
               &incomplete_task_count_, &agent_controller_bindings_,
               agent_.get(),
               [this] {
                 application_controller_.reset();
                 application_services_.reset();
                 agent_.reset();
                 agent_context_binding_.Close();
               },
               [this, callback]() {
                 agent_runner_->RemoveAgent(url_);
                 callback();
               });
}

void AgentContextImpl::NewConnection(
    const std::string& requestor_url,
    fidl::InterfaceRequest<app::ServiceProvider> incoming_services_request,
    fidl::InterfaceRequest<AgentController> agent_controller_request) {
  // First, make sure that the agent is ready.
  MaybeInitializeAgent();

  // Second, on the operation queue - forward the connection request.
  new SyncCall(&operation_queue_, ftl::MakeCopyable([
    this, requestor_url,
    incoming_services_request = std::move(incoming_services_request),
    agent_controller_request = std::move(agent_controller_request)
  ]() mutable {
    agent_->Connect(requestor_url, std::move(incoming_services_request));

    // Add a binding to the |controller|. When all the bindings go away
    // we can stop the agent.
    agent_controller_bindings_.AddBinding(this,
                                          std::move(agent_controller_request));
  }));
}

void AgentContextImpl::NewTask(const std::string& task_id) {
  // First, make sure that the agent is ready.
  MaybeInitializeAgent();

  // Second, on the operation queue - run the task.
  new SyncCall(&operation_queue_, [this, task_id] {
    // Increment the counter for number of incomplete tasks. Decrement it when
    // we receive its callback;
    incomplete_task_count_++;
    agent_->RunTask(task_id, [this] {
      incomplete_task_count_--;
      MaybeStopAgent();
    });
  });
}

void AgentContextImpl::GetComponentContext(
    fidl::InterfaceRequest<ComponentContext> request) {
  component_context_bindings_.AddBinding(&component_context_impl_,
                                         std::move(request));
}

void AgentContextImpl::GetTokenProvider(
    fidl::InterfaceRequest<auth::TokenProvider> request) {
  token_provider_factory_->GetTokenProvider(url_, std::move(request));
}

void AgentContextImpl::GetIntelligenceServices(
    fidl::InterfaceRequest<maxwell::IntelligenceServices> request) {
  auto agent_scope = maxwell::AgentScope::New();
  agent_scope->url = url_;
  auto scope = maxwell::ComponentScope::New();
  scope->set_agent_scope(std::move(agent_scope));
  user_intelligence_provider_->GetComponentIntelligenceServices(
      std::move(scope), std::move(request));
}

void AgentContextImpl::ScheduleTask(TaskInfoPtr task_info) {
  agent_runner_->ScheduleTask(url_, std::move(task_info));
}

void AgentContextImpl::DeleteTask(const fidl::String& task_id) {
  agent_runner_->DeleteTask(url_, task_id);
}

void AgentContextImpl::Done() {}

void AgentContextImpl::MaybeInitializeAgent() {
  // First, verify that the agent is connected.
  new SyncCall(&operation_queue_, [this] {
    if (ready_) {
      // Connection is already present.
      return;
    }

    // Start up the agent process.
    auto launch_info = app::ApplicationLaunchInfo::New();
    launch_info->url = url_;
    launch_info->services = application_services_.NewRequest();
    application_launcher_->CreateApplication(
        std::move(launch_info), application_controller_.NewRequest());
    ConnectToService(application_services_.get(), agent_.NewRequest());

    // When the agent process dies, we remove it.
    // TODO(alhaad): In the future we would want to detect a crashing agent and
    // stop scheduling tasks for it.
    application_controller_.set_connection_error_handler(
        [this] { MaybeStopAgent(); });

    // When all the |AgentController| bindings go away maybe stop the agent.
    agent_controller_bindings_.set_on_empty_set_handler(
        [this] { MaybeStopAgent(); });
  });

  // Second, make sure that the agent is initialized i.e. Agent.Initialize()
  // has returned its callback.
  new InitializeCall(&operation_queue_, &ready_, agent_.get(),
                     &agent_context_binding_, [this] { ready_ = true; });
}

void AgentContextImpl::MaybeStopAgent() {
  new StopCall(&operation_queue_, &ready_, false /* is_terminating */,
               &incomplete_task_count_, &agent_controller_bindings_,
               agent_.get(),
               [this] {
                 application_controller_.reset();
                 application_services_.reset();
                 agent_.reset();
                 agent_context_binding_.Close();
               },
               [this] {
                 // If there are no operation on the operation queue, delete
                 // this agent.
                 agent_runner_->RemoveAgent(url_);
               });
}

}  // namespace modular
