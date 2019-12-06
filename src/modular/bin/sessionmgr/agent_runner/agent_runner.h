// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_BIN_SESSIONMGR_AGENT_RUNNER_AGENT_RUNNER_H_
#define SRC_MODULAR_BIN_SESSIONMGR_AGENT_RUNNER_AGENT_RUNNER_H_

#include <fuchsia/auth/cpp/fidl.h>
#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_ptr_set.h>
#include <lib/sys/inspect/cpp/component.h>

#include <functional>
#include <map>
#include <memory>
#include <string>

#include "src/lib/fxl/macros.h"
#include "src/modular/bin/sessionmgr/agent_runner/agent_service_index.h"
#include "src/modular/bin/sessionmgr/agent_services_factory.h"
#include "src/modular/lib/async/cpp/operation.h"

namespace modular {

// This is the component namespace we give to all agents; used for namespacing
// storage between different component types.
constexpr char kAgentComponentNamespace[] = "agents";

class AgentContextImpl;
class EntityProviderRunner;

// This class provides a way for components to connect to agents and
// manages the life time of a running agent.
// If sessionmgr_context is provided, services from that context can be exposed to agents.
// This is used to make the fuchsia::intl::PropertyProvider available, which may not be necessary
// for some test environments that construct AgentRunner outside of a Sessionmgr.
class AgentRunner {
 public:
  AgentRunner(fuchsia::sys::Launcher* launcher,
              fuchsia::auth::TokenManager* token_manager,
              AgentServicesFactory* agent_services_factory,
              EntityProviderRunner* entity_provider_runner, inspect::Node* session_inspect_node,
              std::unique_ptr<AgentServiceIndex> agent_service_index = nullptr,
              sys::ComponentContext* const sessionmgr_context = nullptr);
  ~AgentRunner();

  // |callback| is called after - (1) all agents have been shutdown and (2)
  // no new tasks are scheduled to run.
  void Teardown(fit::function<void()> callback);

  // Connects to an agent (and starts it up if it doesn't exist) through
  // |fuchsia::modular::Agent.Connect|. Called using
  // fuchsia::modular::ComponentContext.
  void ConnectToAgent(
      const std::string& requestor_url, const std::string& agent_url,
      fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> incoming_services_request,
      fidl::InterfaceRequest<fuchsia::modular::AgentController> agent_controller_request);

  // Supports implementation of ComponentContext/ConnectToAgentService().
  void ConnectToAgentService(const std::string& requestor_url,
                             fuchsia::modular::AgentServiceRequest request);

  // Connects to an agent (and starts it up if it doesn't exist) through its
  // |fuchsia::modular::EntityProvider| service.
  void ConnectToEntityProvider(
      const std::string& agent_url,
      fidl::InterfaceRequest<fuchsia::modular::EntityProvider> entity_provider_request,
      fidl::InterfaceRequest<fuchsia::modular::AgentController> agent_controller_request);

  // Removes an agent. Called by AgentContextImpl when it is done.
  // NOTE: This should NOT take a const reference, since |agent_url| will die
  // the moment we delete |AgentContextImpl|.
  void RemoveAgent(std::string agent_url);

 private:
  // Used by ConnectToAgentService() to connect to the agent (if known) and its
  // named service. Calls ConnectToAgent(), providing a temporary
  // |ServiceProviderPtr| on which to then invoke ConnecToService() with the
  // given service_name and channel.
  //
  // |requestor_url| The URL of the component requesting the service.
  // |agent_url| The URL of the agent believed to provide the service.
  // |agent_controller_request| Returns the object that maintains the requestor
  // connection to the agent.
  // |service_name| The name of the requested service.
  // |channel| The channel associated with the requestor's pending service
  // request, to be used to communicate with the service, once connected.
  void ConnectToService(
      std::string requestor_url, std::string agent_url,
      fidl::InterfaceRequest<fuchsia::modular::AgentController> agent_controller_request,
      std::string service_name, ::zx::channel channel);

  // During ConnectToAgentService, if an agent is not found, close the channel
  // established for the service, and indicate the reason with FIDL epitaph
  // error ZX_ERR_NOT_FOUND.
  void HandleAgentServiceNotFound(::zx::channel channel, std::string service_name);

  // Schedules the agent to start running if it isn't already running (e.g.,
  // it could be not running or in the middle of terminating). Once the agent
  // is in a running state, calls |done|.
  void EnsureAgentIsRunning(const std::string& agent_url, fit::function<void()> done);

  // Actually starts up an agent (used by |EnsureAgentIsRunning()| above).
  void RunAgent(const std::string& agent_url);

  // Will also start and initialize the agent as a consequence.
  void ForwardConnectionsToAgent(const std::string& agent_url);

  // A set of all agents that are either running or scheduled to be run.
  std::vector<std::string> GetAllAgents();

  // agent URL -> pending agent connections
  // This map holds connections to an agent that we hold onto while the
  // existing agent is in a terminating state.
  struct PendingAgentConnectionEntry {
    const std::string requestor_url;
    fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> incoming_services_request;
    fidl::InterfaceRequest<fuchsia::modular::AgentController> agent_controller_request;
  };
  std::map<std::string, std::vector<struct PendingAgentConnectionEntry>> pending_agent_connections_;

  // agent URL -> pending entity provider connection
  // This map holds connections to an agents' fuchsia::modular::EntityProvider
  // that we hold onto while the existing agent is in a terminating state.
  struct PendingEntityProviderConnectionEntry {
    fidl::InterfaceRequest<fuchsia::modular::EntityProvider> entity_provider_request;
    fidl::InterfaceRequest<fuchsia::modular::AgentController> agent_controller_request;
  };
  std::map<std::string, struct PendingEntityProviderConnectionEntry>
      pending_entity_provider_connections_;

  // agent URL -> done callbacks to invoke once agent has started.
  // Holds requests to start an agent; in case an agent is already in a
  // terminating state, we pend those requests here until the agent
  // terminates.
  std::map<std::string, std::vector<fit::function<void()>>> run_agent_callbacks_;

  // agent URL -> modular.fuchsia::modular::AgentContext
  std::map<std::string, std::unique_ptr<AgentContextImpl>> running_agents_;

  fuchsia::sys::Launcher* const launcher_;
  fuchsia::auth::TokenManager* const token_manager_;
  AgentServicesFactory* const agent_services_factory_;
  EntityProviderRunner* const entity_provider_runner_;

  // When this is marked true, no new new tasks will be scheduled.
  std::shared_ptr<bool> terminating_;

  // Not owned. This is the parent node to the agent nodes.
  inspect::Node* session_inspect_node_;

  // May be nullptr or empty.
  std::unique_ptr<AgentServiceIndex> agent_service_index_;

  // The sys::ComponentContext in which SessionmgrImpl was launched (also needed by agents).
  // AgentContext will use this to re-expose services from the "sys" Realm, like
  // fuchsia::intl::PropertyProvider, to agents.
  //
  // This can be a nullptr.
  sys::ComponentContext* const sessionmgr_context_;

  FXL_DISALLOW_COPY_AND_ASSIGN(AgentRunner);
};

}  // namespace modular

#endif  // SRC_MODULAR_BIN_SESSIONMGR_AGENT_RUNNER_AGENT_RUNNER_H_
