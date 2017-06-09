// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/agent_runner/agent_context_impl.h"

#include <memory>

#include "application/lib/app/connect.h"
#include "apps/modular/src/agent_runner/agent_runner.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/mtl/tasks/message_loop.h"

namespace modular {

namespace {
constexpr ftl::TimeDelta kKillTimeout = ftl::TimeDelta::FromSeconds(2);
}

class AgentContextImpl::StartAndInitializeCall : Operation<> {
 public:
  StartAndInitializeCall(OperationContainer* const container,
                         AgentContextImpl* const agent_context_impl)
      : Operation(container, [] {}), agent_context_impl_(agent_context_impl) {
    Ready();
  }

 private:
  void Run() override {
    FTL_CHECK(agent_context_impl_->state_ == State::INITIALIZING);
    FTL_DLOG(INFO) << "Starting new agent, url: " << agent_context_impl_->url_;

    FlowToken flow{this};

    // Start up the agent process.
    auto launch_info = app::ApplicationLaunchInfo::New();
    launch_info->url = agent_context_impl_->url_;
    launch_info->services =
        agent_context_impl_->application_services_.NewRequest();
    agent_context_impl_->application_launcher_->CreateApplication(
        std::move(launch_info),
        agent_context_impl_->application_controller_.NewRequest());
    ConnectToService(agent_context_impl_->application_services_.get(),
                     agent_context_impl_->agent_.NewRequest());

    // When the agent process dies, we remove it.
    // TODO(alhaad): In the future we would want to detect a crashing agent and
    // stop scheduling tasks for it.
    agent_context_impl_->application_controller_.set_connection_error_handler(
        [agent_context_impl = agent_context_impl_] {
          agent_context_impl->MaybeStopAgent();
        });

    // When all the |AgentController| bindings go away maybe stop the agent.
    agent_context_impl_->agent_controller_bindings_.set_on_empty_set_handler(
        [agent_context_impl = agent_context_impl_] {
          agent_context_impl->MaybeStopAgent();
        });

    // TODO(alhaad): We should have a timer for an agent which does not return
    // its callback within some timeout.
    agent_context_impl_->agent_->Initialize(
        agent_context_impl_->agent_context_binding_.NewBinding(),
        [this, flow] { agent_context_impl_->state_ = State::RUNNING; });
  }

  AgentContextImpl* const agent_context_impl_;

  FTL_DISALLOW_COPY_AND_ASSIGN(StartAndInitializeCall);
};

// If |is_terminating| is set to true, the agent will be torn down irrespective
// of whether there is an open-connection or running task. Returns |true| if the
// agent was stopped, false otherwise (could be because agent has pending
// tasks).
class AgentContextImpl::StopCall : Operation<bool> {
 public:
  StopCall(OperationContainer* const container,
           const bool terminating,
           AgentContextImpl* const agent_context_impl,
           ResultCall result_call)
      : Operation(container, std::move(result_call)),
        agent_context_impl_(agent_context_impl),
        terminating_(terminating) {
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this, &stopped_};

    if (agent_context_impl_->state_ == State::TERMINATING) {
      return;
    }

    if (terminating_ ||
        (agent_context_impl_->agent_controller_bindings_.size() == 0 &&
         agent_context_impl_->incomplete_task_count_ == 0)) {
      Stop(flow);
    }
  }

  void Stop(FlowToken flow) {
    agent_context_impl_->state_ = State::TERMINATING;

    // This flow exlusively branches below, so we need to put it in a shared
    // container from which it can be removed once for all branches.
    FlowTokenHolder branch{flow};

    auto kill_agent = [this, branch] {
      std::unique_ptr<FlowToken> flow = branch.Continue();
      if (!flow) {
        return;
      }

      stopped_ = true;
      agent_context_impl_->application_controller_.reset();
      agent_context_impl_->application_services_.reset();
      agent_context_impl_->agent_.reset();
      agent_context_impl_->agent_context_binding_.Close();
    };

    // Whichever of the 3 signals triggers first:
    agent_context_impl_->agent_->Stop(kill_agent);
    agent_context_impl_->application_controller_.set_connection_error_handler(
        kill_agent);
    kill_timer_.Start(mtl::MessageLoop::GetCurrent()->task_runner().get(),
                      kill_agent, kKillTimeout);
  }

  bool stopped_ = false;
  AgentContextImpl* const agent_context_impl_;
  const bool terminating_;  // is the agent runner terminating?
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
                              url,
                              url),
      token_provider_factory_(info.token_provider_factory),
      user_intelligence_provider_(info.user_intelligence_provider) {
  new StartAndInitializeCall(&operation_queue_, this);
}

AgentContextImpl::~AgentContextImpl() = default;

void AgentContextImpl::NewConnection(
    const std::string& requestor_url,
    fidl::InterfaceRequest<app::ServiceProvider> incoming_services_request,
    fidl::InterfaceRequest<AgentController> agent_controller_request) {
  // Queue adding the connection
  new SyncCall(&operation_queue_, ftl::MakeCopyable([
    this, requestor_url,
    incoming_services_request = std::move(incoming_services_request),
    agent_controller_request = std::move(agent_controller_request)
  ]() mutable {
    FTL_CHECK(state_ == State::RUNNING);

    agent_->Connect(requestor_url, std::move(incoming_services_request));

    // Add a binding to the |controller|. When all the bindings go away
    // we can stop the agent.
    agent_controller_bindings_.AddBinding(this,
                                          std::move(agent_controller_request));
  }));
}

void AgentContextImpl::NewTask(const std::string& task_id) {
  new SyncCall(&operation_queue_, [this, task_id] {
    FTL_CHECK(state_ == State::RUNNING);
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

void AgentContextImpl::MaybeStopAgent() {
  new StopCall(&operation_queue_, false /* is agent runner terminating? */,
               this, [this](bool stopped) {
                 if (stopped) {
                   agent_runner_->RemoveAgent(url_);
                   // |this| is no longer valid at this point.
                 }
               });
}

void AgentContextImpl::StopForTeardown() {
  new StopCall(&operation_queue_, true /* is agent runner terminating? */, this,
               [this](bool stopped) {
                 FTL_DCHECK(stopped);
                 agent_runner_->RemoveAgent(url_);
                 // |this| is no longer valid at this point.
               });
}

}  // namespace modular
