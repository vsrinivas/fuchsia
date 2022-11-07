// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_BIN_SESSIONMGR_AGENT_RUNNER_AGENT_CONTEXT_IMPL_H_
#define SRC_MODULAR_BIN_SESSIONMGR_AGENT_RUNNER_AGENT_CONTEXT_IMPL_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/inspect/cpp/component.h>

#include <set>
#include <string>

#include "src/lib/fxl/macros.h"
#include "src/modular/bin/sessionmgr/agent_services_factory.h"
#include "src/modular/bin/sessionmgr/component_context_impl.h"
#include "src/modular/lib/async/cpp/operation.h"
#include "src/modular/lib/deprecated_service_provider/service_provider_impl.h"
#include "src/modular/lib/fidl/app_client.h"

namespace modular {

class AgentRunner;

// The parameters of agent context that do not vary by instance.
struct AgentContextInfo {
  const ComponentContextInfo component_context_info;
  fuchsia::sys::Launcher* const launcher;
  AgentServicesFactory* const agent_services_factory;
  // If sessionmgr_context is nullptr, ignore (do not attempt to forward services).
  sys::ComponentContext* const sessionmgr_context;
};

// This class manages an agent and its life cycle. AgentRunner owns this class,
// and instantiates one for every instance of an agent running. All requests for
// this agent (identified for now by the agent's URL) are routed to this
// class. This class manages all AgentControllers associated with this agent.
class AgentContextImpl : fuchsia::modular::AgentController {
 public:
  // Starts the agent specified in |agent_config| and provides it:
  //  1) A set of services from |info.agent_services_factory| for this agent's url.
  //
  // Enumerates the services exposed in the agent's outgoing directory and makes those available
  // to clients through ConnectToService().
  //
  // If |on_crash| is not null, it will be called if the agent unexpectedly terminates.
  AgentContextImpl(const AgentContextInfo& info, fuchsia::modular::session::AppConfig agent_config,
                   inspect::Node agent_node, std::function<void()> on_crash = nullptr);

  // Manages the lifecycle of the already-running component |app_client| as an agent.
  //
  // Enumerates the services exposed in the agent's outgoing directory and makes those available
  // to clients through ConnectToService().
  //
  // If |on_crash| is not null, it will be called if the agent unexpectedly terminates.
  AgentContextImpl(const AgentContextInfo& info, std::string agent_url,
                   std::unique_ptr<AppClient<fuchsia::modular::Lifecycle>> app_client,
                   inspect::Node agent_node, std::function<void()> on_crash = nullptr);

  // Manages the lifecycle of the already-running agent that exposes the `fuchsia.modular.Agent`
  // protocol at |agent|.
  //
  // If |on_crash| is not null, it will be called if the agent unexpectedly terminates.
  AgentContextImpl(const AgentContextInfo& info, std::string agent_url,
                   fuchsia::modular::AgentPtr agent, inspect::Node agent_node,
                   std::function<void()> on_crash = nullptr);

  ~AgentContextImpl() override;

  // Stops the running agent. Calls into |AgentRunner::RemoveAgent()| to remove itself.
  void StopForTeardown(fit::function<void()> callback);

  // Attempts to connect |channel| to service |service_name| published by the agent.
  // If possible, connects to a service published in the agent's outgoing directory
  // and falls back to using fuchsia.modular.Agent/Connect().
  //
  // If |agent_controller_request| is non-null, tracks its lifecycle and ensures
  // this agent does not stop until |agent_controller_request| has closed.
  void ConnectToService(
      std::string requestor_url,
      fidl::InterfaceRequest<fuchsia::modular::AgentController> agent_controller_request,
      std::string service_name, ::zx::channel channel);

  // Called by AgentRunner when a component wants to connect to this agent.
  // Connections will pend until Agent::Initialize() responds
  // back, at which point all connections will be forwarded to the agent.
  void NewAgentConnection(
      const std::string& requestor_url,
      fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> incoming_services_request,
      fidl::InterfaceRequest<fuchsia::modular::AgentController> agent_controller_request);

  // Lifecycle state of the agent process.
  //
  // INITIALIZING --> RUNNING --> TERMINATING --> TERMINATED
  //                     |                            ^
  //                     +----------------------------+
  enum class State {
    // Initial state. The agent has not started at this point.
    INITIALIZING,

    // Agent component has been started and the context is initialized.
    RUNNING,

    // Agent is being gracefully torn down.
    TERMINATING,

    // Agent component has terminated. This is a terminal state.
    TERMINATED
  };
  State state() { return state_; }

  sys::ServiceDirectory& services() { return app_client_->services(); }

 private:
  // Adds an operation on |operation_queue_| that disconnects from agent protocols
  // and moves the state to TERMINATED.
  //
  // This is meant to be called to handle an unexpected agent component termination,
  // not directly as part of a graceful teardown sequence. However, it still may
  // be executed during teardown (TERMINATING state), in which case it does nothing.
  void StopOnAppError();

  const std::string url_;

  // Client to the agent component, created when the agent starts, and destroyed when
  // the agent is terminated.
  //
  // |app_client_| owns the agent's ComponentController. Destroying it signals that the agent
  // component should be terminated.
  //
  // Exists only in the RUNNING and TERMINATING state when `AgentContextImpl` is constructed
  // with an `AgentConfig` or `AppClient`.
  //
  // Reset to nullptr when TERMINATED, and unset when `AgentContextImpl` is constructed
  // with a `fuchsia::modular::AgentPtr`.
  std::unique_ptr<AppClient<fuchsia::modular::Lifecycle>> app_client_;

  fuchsia::modular::AgentPtr agent_;
  fidl::BindingSet<fuchsia::modular::AgentController> agent_controller_bindings_;

  // The names of services published by the agent in its outgoing directory.
  std::set<std::string> agent_outgoing_services_;

  ComponentContextImpl component_context_impl_;

  // Services provided to the agent in its namespace.
  component::ServiceProviderImpl service_provider_impl_;

  AgentRunner* const agent_runner_;                     // Not owned.
  AgentServicesFactory* const agent_services_factory_;  // Not owned.

  inspect::Node agent_node_;

  // Called if this agent terminates unexpectedly. Can be uninitialized.
  std::function<void()> on_crash_;

  State state_ = State::INITIALIZING;

  OperationQueue operation_queue_;

  // Operations implemented here.
  class InitializeAppClientCall;
  class InitializeAgentPtrCall;
  class StopCall;
  class OnAppErrorCall;

  FXL_DISALLOW_COPY_AND_ASSIGN(AgentContextImpl);
};

}  // namespace modular

#endif  // SRC_MODULAR_BIN_SESSIONMGR_AGENT_RUNNER_AGENT_CONTEXT_IMPL_H_
