// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_BIN_SESSIONMGR_USER_INTELLIGENCE_PROVIDER_IMPL_H_
#define SRC_MODULAR_BIN_SESSIONMGR_USER_INTELLIGENCE_PROVIDER_IMPL_H_

#include <fuchsia/intl/cpp/fidl.h>
#include <fuchsia/modular/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/svc/cpp/service_namespace.h>
#include <lib/svc/cpp/services.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/channel.h>

#include <deque>
#include <string>
#include <vector>

#include "src/modular/bin/sessionmgr/agent_runner/agent_runner.h"
#include "src/modular/bin/sessionmgr/rate_limited_retry.h"

namespace modular {

class UserIntelligenceProviderImpl : public fuchsia::modular::UserIntelligenceProvider {
 public:
  // |context| is not owned and must outlive this instance.
  UserIntelligenceProviderImpl(
      fit::function<void(fidl::InterfaceRequest<fuchsia::modular::FocusProvider>)>
          focus_provider_connector,
      fit::function<void(fidl::InterfaceRequest<fuchsia::modular::PuppetMaster>)>
          puppet_master_connector,
      fit::function<void(fidl::InterfaceRequest<fuchsia::intl::PropertyProvider>)>
          intl_property_provider,
      fit::function<bool()> is_terminating_cb);

  ~UserIntelligenceProviderImpl() override = default;

  void StartAgents(AgentRunner* agent_runner, std::vector<std::string> session_agents,
                   std::vector<std::string> startup_agents);

  // |UserIntelligenceProvider
  void GetServicesForAgent(std::string url, GetServicesForAgentCallback callback) override;

 private:
  struct SessionAgentData {
    struct DeferredInterfaceRequest {
      template <class Interface>
      DeferredInterfaceRequest(fidl::InterfaceRequest<Interface> request);

      const char* name;
      zx::channel channel;
    };

    SessionAgentData();

    template <class Interface>
    void ConnectOrQueueServiceRequest(fidl::InterfaceRequest<Interface> request);

    fuchsia::modular::AgentControllerPtr controller;

    fuchsia::sys::ServiceProviderPtr services;
    // If an agent crashes, there is a period (~1 sec) where its |services|
    // interface is invalid before its controller is closed. During that
    // period, we should queue requests until we've restarted the agent.
    std::vector<DeferredInterfaceRequest> pending_service_requests;

    modular::RateLimitedRetry restart;
  };

  using ServiceProviderInitializer =
      fit::function<void(const std::string& url, component::ServiceNamespace* agent_host)>;
  // A ServiceProviderInitializer that adds standard agent services, including
  // attributed context entry point. Returns the names
  // of the services added.
  std::vector<std::string> AddAgentServices(const std::string& url,
                                            component::ServiceNamespace* agent_host);

  void StartAgent(AgentRunner* agent_runner, const std::string& url);

  void StartSessionAgent(AgentRunner* agent_runner, const std::string& url);

  std::map<std::string, SessionAgentData> session_agents_;

  fidl::InterfacePtr<fuchsia::modular::ComponentContext> component_context_;
  fidl::InterfacePtr<fuchsia::modular::StoryProvider> story_provider_;
  fidl::InterfacePtr<fuchsia::modular::FocusProvider> focus_provider_;
  fidl::InterfacePtr<fuchsia::intl::PropertyProvider> property_provider_;

  fit::function<void(fidl::InterfaceRequest<fuchsia::modular::FocusProvider>)>
      focus_provider_connector_;
  fit::function<void(fidl::InterfaceRequest<fuchsia::modular::PuppetMaster>)>
      puppet_master_connector_;
  fit::function<void(fidl::InterfaceRequest<fuchsia::intl::PropertyProvider>)>
      intl_property_provider_connector_;

  // Return |true| to avoid automatically restarting session_agents_.
  fit::function<bool()> is_terminating_cb_ = nullptr;

  // Framework fuchsia::modular::Agent controllers. Hanging onto these tells the
  // Framework we want the Agents to keep running.
  std::vector<fuchsia::modular::AgentControllerPtr> agent_controllers_;

  // ServiceNamespace(s) backing the services provided to these agents via its
  // namespace.
  std::deque<component::ServiceNamespace> agent_namespaces_;
};

}  // namespace modular

#endif  // SRC_MODULAR_BIN_SESSIONMGR_USER_INTELLIGENCE_PROVIDER_IMPL_H_
