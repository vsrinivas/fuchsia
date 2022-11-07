// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_BIN_SESSIONMGR_AGENT_RUNNER_AGENT_RUNNER_H_
#define SRC_MODULAR_BIN_SESSIONMGR_AGENT_RUNNER_AGENT_RUNNER_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_ptr_set.h>
#include <lib/sys/inspect/cpp/component.h>

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>

#include "src/lib/fxl/macros.h"
#include "src/modular/bin/sessionmgr/agent_services_factory.h"
#include "src/modular/lib/fidl/app_client.h"
#include "src/modular/lib/modular_config/modular_config_accessor.h"

namespace component {
class ServiceProviderImpl;
}

namespace modular {

class AgentContextImpl;

struct AgentServiceEntry {
  std::string agent_url;
  std::string expose_from;
};

// This class provides a way for components to connect to agents and
// manages the life time of a running agent.
class AgentRunner {
 public:
  // If |sessionmgr_context| is provided, fuchsia.intl.PropertyProvider is exposed to agents.
  // |on_critical_agent_crash| is called when a "critical agent" (all agents
  // with entries in |restart_session_on_agent_crash|). It is expected to
  // restart the session.
  AgentRunner(const ModularConfigAccessor* config_accessor, fuchsia::sys::Launcher* launcher,
              AgentServicesFactory* agent_services_factory, inspect::Node* session_inspect_node,
              std::function<void()> on_critical_agent_crash,
              std::map<std::string, AgentServiceEntry> agent_service_index = {},
              std::vector<std::string> session_agents = {},
              std::vector<std::string> restart_session_on_agent_crash = {},
              sys::ComponentContext* sessionmgr_context = nullptr);
  ~AgentRunner();

  // |callback| is called after - (1) all agents have been shutdown and (2)
  // no new tasks are scheduled to run.
  void Teardown(fit::function<void()> callback);

  // Returns a list of service names present in the cached agent service index.
  std::vector<std::string> GetAgentServices() const;

  // Returns true if the `agent_url` is present in the agent service index.
  bool AgentInServiceIndex(const std::string& agent_url) const;

  // Publishes all services in |agent_service_index_| to |service_provider|, allowing clients
  // of |service_provider|, provided |service_provider| is bound to a component's environment,
  // to connect to agent services directly through that environment.
  void PublishAgentServices(const std::string& requestor_url,
                            component::ServiceProviderImpl* service_provider);

  // Adds a component that is already running (or in the process of starting) to the
  // list of agents managed by AgentRunner.
  void AddRunningAgent(std::string agent_url,
                       std::unique_ptr<AppClient<fuchsia::modular::Lifecycle>> app_client);

  // Adds an agent that is already running and exposes the `fuchsia.modular.Agent` protocol
  // at `agent` to list of agents managed by AgentRunner.
  void AddAgentFromService(std::string agent_url, fuchsia::modular::AgentPtr agent);

  // Connects to an agent (and starts it up if it doesn't exist) through
  // |Agent.Connect|. Called using ComponentContext.
  void ConnectToAgent(
      std::string requestor_url, std::string agent_url,
      fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> incoming_services_request,
      fidl::InterfaceRequest<fuchsia::modular::AgentController> agent_controller_request);

  // Supports implementation of ComponentContext/ConnectToAgentService().
  void ConnectToAgentService(std::string requestor_url,
                             fuchsia::modular::AgentServiceRequest request);

  // Return the outgoing Services from a running agent.
  std::optional<std::reference_wrapper<sys::ServiceDirectory>> GetAgentOutgoingServices(
      const std::string& agent_url);

  // Removes an agent. Called by AgentContextImpl when it is done.
  // NOTE: This should NOT take a const reference, since |agent_url| will die
  // the moment we delete |AgentContextImpl|.
  void RemoveAgent(const std::string& agent_url);

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
      std::string service_name, zx::channel channel);

  // During ConnectToAgentService, if an agent is not found, close the channel
  // established for the service, and indicate the reason with FIDL epitaph
  // error ZX_ERR_NOT_FOUND.
  static void HandleAgentServiceNotFound(zx::channel channel, const std::string& service_name);

  // Schedules the agent to start running if it isn't already running (e.g.,
  // it could be not running or in the middle of terminating). Once the agent
  // is in a running state, calls |done|.
  void EnsureAgentIsRunning(const std::string& agent_url,
                            fit::function<void(const std::string&)> done);

  // Actually starts up an agent (used by |EnsureAgentIsRunning()| above).
  void RunAgent(const std::string& agent_url);

  // A set of all agents that are either running or scheduled to be run.
  std::vector<std::string> GetAllAgents();

  const ModularConfigAccessor* config_accessor_;

  // agent URL -> done callbacks to invoke once agent has started.
  // Holds requests to start an agent; in case an agent is already in a
  // terminating state, we pend those requests here until the agent
  // terminates.
  std::map<std::string, std::vector<fit::function<void(const std::string&)>>> run_agent_callbacks_;

  // agent URL -> modular.fuchsia::modular::AgentContext
  std::map<std::string, std::unique_ptr<AgentContextImpl>> running_agents_;

  fuchsia::sys::Launcher* const launcher_;
  AgentServicesFactory* const agent_services_factory_;

  // When this is marked true, no new new tasks will be scheduled.
  std::shared_ptr<bool> terminating_;

  // Not owned. This is the parent node to the agent nodes.
  inspect::Node* session_inspect_node_;

  // Called when an agent listed in |restart_session_on_agent_crash_|
  // terminates.
  std::function<void()> on_critical_agent_crash_;

  // Services mapped to agents that provide those services. Used when a service is requested
  // without specifying the handling agent. May be empty.
  std::map<std::string, AgentServiceEntry> agent_service_index_;

  // The session agents specified in the modular configuration.
  std::vector<std::string> session_agents_;

  // The agent URLs specified in the Modular configuration that should trigger
  // a session restart on termination.
  //
  // This requires that |session_context_| is not a nullptr.
  std::vector<std::string> restart_session_on_agent_crash_;

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
