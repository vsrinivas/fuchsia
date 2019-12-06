// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_BIN_SESSIONMGR_AGENT_RUNNER_AGENT_CONTEXT_IMPL_H_
#define SRC_MODULAR_BIN_SESSIONMGR_AGENT_RUNNER_AGENT_CONTEXT_IMPL_H_

#include <fuchsia/auth/cpp/fidl.h>
#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/inspect/cpp/component.h>

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
  fuchsia::auth::TokenManager* const token_manager;
  AgentServicesFactory* const agent_services_factory;
  // If sessionmgr_context is nullptr, ignore (do not attempt to forward services).
  sys::ComponentContext* const sessionmgr_context;
};

// This class manages an agent and its life cycle. AgentRunner owns this class,
// and instantiates one for every instance of an agent running. All requests for
// this agent (identified for now by the agent's URL) are routed to this
// class. This class manages all AgentControllers associated with this agent.
class AgentContextImpl : fuchsia::modular::AgentContext,
                         fuchsia::modular::AgentController,
                         fuchsia::auth::TokenManager {
 public:
  // Starts the agent specified in |agent_config| and provides it:
  //  1) AgentContext service
  //  2) A set of services from |info.agent_services_factory| for this agent's url.
  explicit AgentContextImpl(const AgentContextInfo& info, fuchsia::modular::AppConfig agent_config,
                            inspect::Node agent_node);
  ~AgentContextImpl() override;

  // Stops the running agent, irrespective of whether there are active
  // AgentControllers. Calls into |AgentRunner::RemoveAgent()| to remove itself.
  void StopForTeardown(fit::function<void()> callback);

  // Called by AgentRunner when a component wants to connect to this agent.
  // Connections will pend until fuchsia::modular::Agent::Initialize() responds
  // back, at which point all connections will be forwarded to the agent.
  void NewAgentConnection(
      const std::string& requestor_url,
      fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> incoming_services_request,
      fidl::InterfaceRequest<fuchsia::modular::AgentController> agent_controller_request);

  // Called by AgentRunner when the framework wants to talk to the
  // |fuchsia::modular::EntityProvider| service from this agent. Similar to
  // NewAgentConnection(), this operation will pend until the entity provider
  // agent is initialized.
  void NewEntityProviderConnection(
      fidl::InterfaceRequest<fuchsia::modular::EntityProvider> entity_provider_request,
      fidl::InterfaceRequest<fuchsia::modular::AgentController> agent_controller_request);

  enum class State { INITIALIZING, RUNNING, TERMINATING };
  State state() { return state_; }

 private:
  // |fuchsia::modular::AgentContext|
  void GetComponentContext(
      fidl::InterfaceRequest<fuchsia::modular::ComponentContext> request) override;
  // |fuchsia::modular::AgentContext|
  void GetTokenManager(fidl::InterfaceRequest<fuchsia::auth::TokenManager> request) override;

  // |fuchsia::modular::AgentContext|
  void GetEntityReferenceFactory(
      fidl::InterfaceRequest<fuchsia::modular::EntityReferenceFactory> request) override;

  // |fuchsia::auth::TokenManager|
  void Authorize(fuchsia::auth::AppConfig app_config,
                 fidl::InterfaceHandle<fuchsia::auth::AuthenticationUIContext> auth_ui_context,
                 std::vector<::std::string> app_scopes, fidl::StringPtr user_profile_id,
                 fidl::StringPtr auth_code, AuthorizeCallback callback) override;

  // |fuchsia::auth::TokenManager|
  void GetAccessToken(fuchsia::auth::AppConfig app_config, std::string user_profile_id,
                      std::vector<::std::string> app_scopes,
                      GetAccessTokenCallback callback) override;

  // |fuchsia::auth::TokenManager|
  void GetIdToken(fuchsia::auth::AppConfig app_config, std::string user_profile_id,
                  fidl::StringPtr audience, GetIdTokenCallback callback) override;

  // |fuchsia::auth::TokenManager|
  void DeleteAllTokens(fuchsia::auth::AppConfig app_config, std::string user_profile_id, bool force,
                       DeleteAllTokensCallback callback) override;

  // |fuchsia::auth::TokenManager|
  void ListProfileIds(fuchsia::auth::AppConfig app_config,
                      ListProfileIdsCallback callback) override;

  // Adds an operation on |operation_queue_|. This operation is immediately
  // Done() if this agent is not |ready_|. Else if there are no active
  // AgentControllers, fuchsia::modular::Agent.Stop() is called with a timeout.
  void StopAgentIfIdle();

  const std::string url_;

  std::unique_ptr<AppClient<fuchsia::modular::Lifecycle>> app_client_;
  fuchsia::modular::AgentPtr agent_;
  fidl::BindingSet<fuchsia::modular::AgentContext> agent_context_bindings_;
  fidl::BindingSet<fuchsia::modular::AgentController> agent_controller_bindings_;
  fidl::BindingSet<fuchsia::auth::TokenManager> token_manager_bindings_;

  AgentRunner* const agent_runner_;

  ComponentContextImpl component_context_impl_;

  // A service provider that represents the services to be added into an
  // application's namespace.
  component::ServiceProviderImpl service_provider_impl_;

  fuchsia::auth::TokenManager* const token_manager_;                              // Not owned.
  EntityProviderRunner* const entity_provider_runner_;                            // Not owned.
  AgentServicesFactory* const agent_services_factory_;                            // Not owned.

  inspect::Node agent_node_;

  State state_ = State::INITIALIZING;

  OperationQueue operation_queue_;

  // Operations implemented here.
  class InitializeCall;
  class StopCall;

  FXL_DISALLOW_COPY_AND_ASSIGN(AgentContextImpl);
};

}  // namespace modular

#endif  // SRC_MODULAR_BIN_SESSIONMGR_AGENT_RUNNER_AGENT_CONTEXT_IMPL_H_
