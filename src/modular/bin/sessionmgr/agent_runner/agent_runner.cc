// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/agent_runner/agent_runner.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fidl/epitaph.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include <map>
#include <set>
#include <utility>

#include "lib/sys/cpp/service_directory.h"
#include "src/modular/bin/sessionmgr/agent_runner/agent_context_impl.h"

namespace modular {

constexpr zx::duration kTeardownTimeout = zx::sec(3);

AgentRunner::AgentRunner(const ModularConfigAccessor* config_accessor,
                         fuchsia::sys::Launcher* const launcher,
                         AgentServicesFactory* const agent_services_factory,
                         inspect::Node* const session_inspect_node,
                         std::function<void()> on_critical_agent_crash,
                         std::map<std::string, AgentServiceEntry> agent_service_index,
                         std::vector<std::string> session_agents,
                         std::vector<std::string> restart_session_on_agent_crash,
                         sys::ComponentContext* const sessionmgr_context)
    : config_accessor_(config_accessor),
      launcher_(launcher),
      agent_services_factory_(agent_services_factory),
      terminating_(std::make_shared<bool>(false)),
      session_inspect_node_(session_inspect_node),
      on_critical_agent_crash_(std::move(on_critical_agent_crash)),
      agent_service_index_(std::move(agent_service_index)),
      session_agents_(std::move(session_agents)),
      restart_session_on_agent_crash_(std::move(restart_session_on_agent_crash)),
      sessionmgr_context_(sessionmgr_context) {}

AgentRunner::~AgentRunner() = default;

void AgentRunner::Teardown(fit::function<void()> callback) {
  // No new agents will be scheduled to run.
  *terminating_ = true;

  FX_LOGS(INFO) << "AgentRunner::Teardown() " << running_agents_.size() << " agents";

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
          FX_LOGS(ERROR) << "AgentRunner::Teardown() timed out";
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

std::vector<std::string> AgentRunner::GetAgentServices() const {
  std::vector<std::string> service_names;
  service_names.reserve(agent_service_index_.size());
  for (const auto& index_entry : agent_service_index_) {
    service_names.push_back(index_entry.first);
  }
  return service_names;
}

void AgentRunner::PublishAgentServices(const std::string& requestor_url,
                                       component::ServiceProviderImpl* service_provider) {
  for (const auto& index_entry : agent_service_index_) {
    const auto& service_name = index_entry.first;
    service_provider->AddServiceForName(
        [this, requestor_url, service_name](zx::channel channel) mutable {
          fuchsia::modular::AgentServiceRequest agent_service_request;
          agent_service_request.set_service_name(service_name);
          agent_service_request.set_channel(std::move(channel));
          ConnectToAgentService(requestor_url, std::move(agent_service_request));
        },
        service_name);
  }
}

void AgentRunner::AddRunningAgent(
    std::string agent_url, std::unique_ptr<AppClient<fuchsia::modular::Lifecycle>> app_client) {
  // Start the agent and issue all callbacks.
  ComponentContextInfo component_info = {this, session_agents_};
  AgentContextInfo info = {component_info, launcher_, agent_services_factory_, sessionmgr_context_};
  fuchsia::modular::session::AppConfig agent_config;
  agent_config.set_url(agent_url);

  // AgentContextImpl will call on_critical_agent_crash if this agent is listed
  // in |restart_session_on_agent_crash_| and terminates unexpectedly.
  bool restart_session_on_crash =
      !config_accessor_->sessionmgr_config().disable_agent_restart_on_crash() ||
      std::find(restart_session_on_agent_crash_.begin(), restart_session_on_agent_crash_.end(),
                agent_url) != restart_session_on_agent_crash_.end();

  FX_CHECK(
      running_agents_
          .emplace(agent_url, std::make_unique<AgentContextImpl>(
                                  info, agent_url, std::move(app_client),
                                  session_inspect_node_->CreateChild(agent_url),
                                  restart_session_on_crash ? on_critical_agent_crash_ : nullptr))
          .second);
}

void AgentRunner::AddAgentFromService(std::string agent_url, fuchsia::modular::AgentPtr agent) {
  // Start the agent and issue all callbacks.
  ComponentContextInfo component_info = {this, session_agents_};
  AgentContextInfo info = {component_info, launcher_, agent_services_factory_, sessionmgr_context_};
  fuchsia::modular::session::AppConfig agent_config;
  agent_config.set_url(agent_url);

  // AgentContextImpl will call on_critical_agent_crash if this agent is listed
  // in |restart_session_on_agent_crash_| and terminates unexpectedly.
  bool restart_session_on_crash =
      !config_accessor_->sessionmgr_config().disable_agent_restart_on_crash() ||
      std::find(restart_session_on_agent_crash_.begin(), restart_session_on_agent_crash_.end(),
                agent_url) != restart_session_on_agent_crash_.end();

  FX_CHECK(
      running_agents_
          .emplace(agent_url, std::make_unique<AgentContextImpl>(
                                  info, agent_url, std::move(agent),
                                  session_inspect_node_->CreateChild(agent_url),
                                  restart_session_on_crash ? on_critical_agent_crash_ : nullptr))
          .second);
}

void AgentRunner::EnsureAgentIsRunning(const std::string& agent_url,
                                       fit::function<void(const std::string&)> done) {
  // Drop all new requests if AgentRunner is terminating.
  if (*terminating_) {
    return;
  }

  auto agent_it = running_agents_.find(agent_url);
  if (agent_it != running_agents_.end()) {
    switch (agent_it->second->state()) {
      case AgentContextImpl::State::INITIALIZING:
      case AgentContextImpl::State::RUNNING:
        // Agent is already running, so we can issue the
        // callback immediately.
        done(agent_url);
        break;
      case AgentContextImpl::State::TERMINATING:
      case AgentContextImpl::State::TERMINATED:
        run_agent_callbacks_[agent_url].push_back(std::move(done));
        break;
    }
    return;
  }
  run_agent_callbacks_[agent_url].push_back(std::move(done));

  RunAgent(agent_url);
}

void AgentRunner::RunAgent(const std::string& agent_url) {
  // Start the agent and issue all callbacks.
  ComponentContextInfo component_info = {this, session_agents_};
  AgentContextInfo info = {component_info, launcher_, agent_services_factory_, sessionmgr_context_};
  fuchsia::modular::session::AppConfig agent_config;
  agent_config.set_url(agent_url);

  // AgentContextImpl will call on_critical_agent_crash if this agent is listed
  // in |restart_session_on_agent_crash_| and terminates unexpectedly.
  bool restart_session_on_crash =
      std::find(restart_session_on_agent_crash_.begin(), restart_session_on_agent_crash_.end(),
                agent_url) != restart_session_on_agent_crash_.end();

  FX_CHECK(
      running_agents_
          .emplace(agent_url,
                   std::make_unique<AgentContextImpl>(
                       info, std::move(agent_config), session_inspect_node_->CreateChild(agent_url),
                       restart_session_on_crash ? on_critical_agent_crash_ : nullptr))
          .second);

  auto run_callbacks_it = run_agent_callbacks_.find(agent_url);
  if (run_callbacks_it != run_agent_callbacks_.end()) {
    for (auto& callback : run_callbacks_it->second) {
      callback(agent_url);
    }
    run_agent_callbacks_.erase(agent_url);
  }
}

void AgentRunner::ConnectToAgent(
    std::string requestor_url, std::string agent_url,
    fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> incoming_services_request,
    fidl::InterfaceRequest<fuchsia::modular::AgentController> agent_controller_request) {
  if (!incoming_services_request) {
    FX_LOGS(ERROR) << "Invalid fuchsia.sys.ServiceProvider handle provided to "
                      "ConnectToAgent() by "
                   << requestor_url;
  }
  EnsureAgentIsRunning(
      std::move(agent_url), [this, requestor_url = std::move(requestor_url),
                             incoming_services_request = std::move(incoming_services_request),
                             agent_controller_request = std::move(agent_controller_request)](
                                const std::string& agent_url) mutable {
        running_agents_[agent_url]->NewAgentConnection(std::move(requestor_url),
                                                       std::move(incoming_services_request),
                                                       std::move(agent_controller_request));
      });
}

void AgentRunner::HandleAgentServiceNotFound(zx::channel channel, const std::string& service_name) {
  FX_LOGS(ERROR) << "No agent found for requested service_name: " << service_name;
  zx_status_t status = fidl_epitaph_write(channel.get(), ZX_ERR_NOT_FOUND);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Error writing epitaph ZX_ERR_NOT_FOUND to channel. Status: "
                   << zx_status_get_string(status);
  }
}

void AgentRunner::ConnectToService(
    std::string requestor_url, std::string agent_url,
    fidl::InterfaceRequest<fuchsia::modular::AgentController> agent_controller_request,
    std::string service_name, zx::channel channel) {
  EnsureAgentIsRunning(
      std::move(agent_url), [this, requestor_url = std::move(requestor_url),
                             service_name = std::move(service_name), channel = std::move(channel),
                             agent_controller_request = std::move(agent_controller_request)](
                                const std::string& agent_url) mutable {
        running_agents_[agent_url]->ConnectToService(
            requestor_url, std::move(agent_controller_request), service_name, std::move(channel));
      });
}

void AgentRunner::ConnectToAgentService(std::string requestor_url,
                                        fuchsia::modular::AgentServiceRequest request) {
  // Drop all new requests if AgentRunner is terminating.
  if (*terminating_) {
    return;
  }

  if (!request.has_service_name()) {
    FX_LOGS(ERROR) << "Missing required service_name in AgentServiceRequest";
    return;
  }

  if (!request.has_channel()) {
    FX_LOGS(ERROR) << "Missing required channel in AgentServiceRequest";
    return;
  }

  std::string agent_url;
  std::string service_name = request.service_name();
  if (request.has_handler()) {
    agent_url = request.handler();
  } else {
    auto it = agent_service_index_.find(request.service_name());
    if (it != agent_service_index_.end()) {
      agent_url = it->second.agent_url;
      service_name = it->second.expose_from;
    } else {
      HandleAgentServiceNotFound(std::move(*request.mutable_channel()), request.service_name());
      return;
    }
  }

  ConnectToService(std::move(requestor_url), std::move(agent_url),
                   std::move(*request.mutable_agent_controller()), service_name,
                   std::move(*request.mutable_channel()));
}

std::optional<std::reference_wrapper<sys::ServiceDirectory>> AgentRunner::GetAgentOutgoingServices(
    const std::string& agent_url) {
  auto agent_it = running_agents_.find(agent_url);
  if (agent_it == running_agents_.end()) {
    return {};
  }

  return agent_it->second->services();
}

bool AgentRunner::AgentInServiceIndex(const std::string& agent_url) const {
  auto it =
      std::find_if(agent_service_index_.begin(), agent_service_index_.end(),
                   [agent_url](const auto& entry) { return entry.second.agent_url == agent_url; });
  return it != agent_service_index_.end();
}

void AgentRunner::RemoveAgent(const std::string& agent_url) {
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

}  // namespace modular
