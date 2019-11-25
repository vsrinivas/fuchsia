// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/agent_runner/agent_runner.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fidl/epitaph.h>
#include <zircon/status.h>

#include <map>
#include <set>
#include <utility>

#include "src/lib/fsl/vmo/strings.h"
#include "src/modular/bin/sessionmgr/agent_runner/agent_context_impl.h"
#include "src/modular/bin/sessionmgr/storage/constants_and_utils.h"
#include "src/modular/lib/fidl/array_to_string.h"
#include "src/modular/lib/fidl/json_xdr.h"

namespace modular {

constexpr zx::duration kTeardownTimeout = zx::sec(3);

AgentRunner::AgentRunner(
    fuchsia::sys::Launcher* const launcher, fuchsia::auth::TokenManager* const token_manager,
    fuchsia::modular::UserIntelligenceProvider* const user_intelligence_provider,
    EntityProviderRunner* const entity_provider_runner, inspect::Node* session_inspect_node,
    std::unique_ptr<AgentServiceIndex> agent_service_index,
    sys::ComponentContext* const sessionmgr_context)
    : launcher_(launcher),
      token_manager_(token_manager),
      user_intelligence_provider_(user_intelligence_provider),
      entity_provider_runner_(entity_provider_runner),
      terminating_(std::make_shared<bool>(false)),
      session_inspect_node_(session_inspect_node),
      agent_service_index_(std::move(agent_service_index)),
      sessionmgr_context_(sessionmgr_context) {}

AgentRunner::~AgentRunner() = default;

void AgentRunner::Teardown(fit::function<void()> callback) {
  // No new agents will be scheduled to run.
  *terminating_ = true;

  FXL_LOG(INFO) << "AgentRunner::Teardown() " << running_agents_.size() << " agents";

  // No agents were running, we are good to go.
  if (running_agents_.empty()) {
    callback();
    return;
  }

  // This is called when agents are done being removed
  auto called = std::make_shared<bool>(false);
  fit::function<void(const bool)> termination_callback =
      [called, callback = std::move(callback)](const bool from_timeout) mutable {
        if (*called) {
          return;
        }

        *called = true;

        if (from_timeout) {
          FXL_LOG(ERROR) << "AgentRunner::Teardown() timed out";
        }

        callback();
        callback = nullptr;  // make sure we release any captured resources
      };

  // Pass a shared copy of "termination_callback" fit::function so
  // we can give it to multiple running_agents. Only the last remaining
  // running_agent will call it.
  for (auto& it : running_agents_) {
    // The running agent will call |AgentRunner::RemoveAgent()| to remove itself
    // from the agent runner. When all agents are done being removed,
    // |termination_callback| will be executed.
    it.second->StopForTeardown(
        [this, termination_callback = termination_callback.share()]() mutable {
          if (running_agents_.empty()) {
            termination_callback(/* from_timeout= */ false);
          }
        });
  }

  async::PostDelayedTask(
      async_get_default_dispatcher(),
      [termination_callback = std::move(termination_callback)]() mutable {
        termination_callback(/* from_timeout= */ true);
      },
      kTeardownTimeout);
}

void AgentRunner::EnsureAgentIsRunning(const std::string& agent_url, fit::function<void()> done) {
  auto agent_it = running_agents_.find(agent_url);
  if (agent_it != running_agents_.end()) {
    if (agent_it->second->state() == AgentContextImpl::State::TERMINATING) {
      run_agent_callbacks_[agent_url].push_back(std::move(done));
    } else {
      // fuchsia::modular::Agent is already running, so we can issue the
      // callback immediately.
      done();
    }
    return;
  }
  run_agent_callbacks_[agent_url].push_back(std::move(done));

  RunAgent(agent_url);
}

void AgentRunner::RunAgent(const std::string& agent_url) {
  // Start the agent and issue all callbacks.
  ComponentContextInfo component_info = {this, entity_provider_runner_};
  AgentContextInfo info = {component_info, launcher_, token_manager_, user_intelligence_provider_,
                           sessionmgr_context_};
  fuchsia::modular::AppConfig agent_config;
  agent_config.url = agent_url;

  FXL_CHECK(running_agents_
                .emplace(agent_url, std::make_unique<AgentContextImpl>(
                                        info, std::move(agent_config),
                                        session_inspect_node_->CreateChild(agent_url)))
                .second);

  auto run_callbacks_it = run_agent_callbacks_.find(agent_url);
  if (run_callbacks_it != run_agent_callbacks_.end()) {
    for (auto& callback : run_callbacks_it->second) {
      callback();
    }
    run_agent_callbacks_.erase(agent_url);
  }
}

void AgentRunner::ConnectToAgent(
    const std::string& requestor_url, const std::string& agent_url,
    fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> incoming_services_request,
    fidl::InterfaceRequest<fuchsia::modular::AgentController> agent_controller_request) {
  // Drop all new requests if AgentRunner is terminating.
  if (*terminating_) {
    return;
  }
  pending_agent_connections_[agent_url].push_back(
      {requestor_url, std::move(incoming_services_request), std::move(agent_controller_request)});
  EnsureAgentIsRunning(agent_url, [this, agent_url] {
    // If the agent was terminating and has restarted, forwarding connections
    // here is redundant, since it was already forwarded earlier.
    ForwardConnectionsToAgent(agent_url);
  });
}

void AgentRunner::HandleAgentServiceNotFound(::zx::channel channel, std::string service_name) {
  FXL_LOG(ERROR) << "No agent found for requested service_name: " << service_name;
  zx_status_t status = fidl_epitaph_write(channel.get(), ZX_ERR_NOT_FOUND);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Error writing epitaph ZX_ERR_NOT_FOUND to channel. Status: "
                   << zx_status_get_string(status);
  }
}

void AgentRunner::ConnectToService(
    std::string requestor_url, std::string agent_url,
    fidl::InterfaceRequest<fuchsia::modular::AgentController> agent_controller_request,
    std::string service_name, ::zx::channel channel) {
  fuchsia::sys::ServiceProviderPtr agent_services;
  ConnectToAgent(requestor_url, agent_url, agent_services.NewRequest(),
                 std::move(agent_controller_request));
  agent_services->ConnectToService(service_name, std::move(channel));
}

void AgentRunner::ConnectToAgentService(const std::string& requestor_url,
                                        fuchsia::modular::AgentServiceRequest request) {
  // Drop all new requests if AgentRunner is terminating.
  if (*terminating_) {
    return;
  }

  if (!request.has_service_name()) {
    FXL_LOG(ERROR) << "Missing required service_name in AgentServiceRequest";
    return;
  }

  if (!request.has_channel()) {
    FXL_LOG(ERROR) << "Missing required channel in AgentServiceRequest";
    return;
  }

  if (!request.has_agent_controller()) {
    FXL_LOG(ERROR) << "Missing required agent_controller in AgentServiceRequest";
    return;
  }

  std::string agent_url;
  if (request.has_handler()) {
    agent_url = request.handler();
  } else {
    if (auto optional = agent_service_index_->FindAgentForService(request.service_name())) {
      agent_url = optional.value();
    } else {
      HandleAgentServiceNotFound(std::move(*request.mutable_channel()), request.service_name());
      return;
    }
  }

  ConnectToService(requestor_url, agent_url, std::move(*request.mutable_agent_controller()),
                   request.service_name(), std::move(*request.mutable_channel()));
}

void AgentRunner::ConnectToEntityProvider(
    const std::string& agent_url,
    fidl::InterfaceRequest<fuchsia::modular::EntityProvider> entity_provider_request,
    fidl::InterfaceRequest<fuchsia::modular::AgentController> agent_controller_request) {
  // Drop all new requests if AgentRunner is terminating.
  if (*terminating_) {
    return;
  }

  pending_entity_provider_connections_[agent_url] = {std::move(entity_provider_request),
                                                     std::move(agent_controller_request)};

  EnsureAgentIsRunning(agent_url, [this, agent_url] {
    auto it = pending_entity_provider_connections_.find(agent_url);
    FXL_DCHECK(it != pending_entity_provider_connections_.end());
    running_agents_[agent_url]->NewEntityProviderConnection(
        std::move(it->second.entity_provider_request),
        std::move(it->second.agent_controller_request));
    pending_entity_provider_connections_.erase(it);
  });
}

void AgentRunner::RemoveAgent(const std::string agent_url) {
  running_agents_.erase(agent_url);

  if (*terminating_) {
    return;
  }

  // At this point, if there are pending requests to start the agent (because
  // the previous one was in a terminating state), we can start it up again.
  if (run_agent_callbacks_.find(agent_url) != run_agent_callbacks_.end()) {
    RunAgent(agent_url);
  }
}

void AgentRunner::ForwardConnectionsToAgent(const std::string& agent_url) {
  // Did we hold onto new connections as the previous one was exiting?
  auto found_it = pending_agent_connections_.find(agent_url);
  if (found_it != pending_agent_connections_.end()) {
    AgentContextImpl* agent = running_agents_[agent_url].get();
    for (auto& pending_connection : found_it->second) {
      agent->NewAgentConnection(pending_connection.requestor_url,
                                std::move(pending_connection.incoming_services_request),
                                std::move(pending_connection.agent_controller_request));
    }
    pending_agent_connections_.erase(found_it);
  }
}

}  // namespace modular
