// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_USER_RUNNER_USER_INTELLIGENCE_PROVIDER_IMPL_H_
#define PERIDOT_BIN_USER_RUNNER_USER_INTELLIGENCE_PROVIDER_IMPL_H_

#include <deque>
#include <string>
#include <vector>

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/svc/cpp/services.h>
#include <lib/zx/channel.h>

#include "peridot/bin/user_runner/agent_launcher.h"
#include "peridot/lib/util/rate_limited_retry.h"

namespace modular {

class UserIntelligenceProviderImpl
    : public fuchsia::modular::UserIntelligenceProvider {
 public:
  // |context| is not owned and must outlive this instance.
  UserIntelligenceProviderImpl(
      component::StartupContext* context,
      fidl::InterfaceHandle<fuchsia::modular::ContextEngine>
          context_engine_handle,
      fidl::InterfaceHandle<fuchsia::modular::StoryProvider>
          story_provider_handle,
      fidl::InterfaceHandle<fuchsia::modular::FocusProvider>
          focus_provider_handle,
      fidl::InterfaceHandle<fuchsia::modular::VisibleStoriesProvider>
          visible_stories_provider_handle,
      fidl::InterfaceHandle<fuchsia::modular::PuppetMaster>
          puppet_master_handle);
  ~UserIntelligenceProviderImpl() override = default;

  void GetComponentIntelligenceServices(
      fuchsia::modular::ComponentScope scope,
      fidl::InterfaceRequest<fuchsia::modular::IntelligenceServices> request)
      override;

  void GetSuggestionProvider(
      fidl::InterfaceRequest<fuchsia::modular::SuggestionProvider> request)
      override;

  void GetSpeechToText(
      fidl::InterfaceRequest<fuchsia::speech::SpeechToText> request) override;

  void StartAgents(fidl::InterfaceHandle<fuchsia::modular::ComponentContext>
                       component_context_handle,
                   fidl::VectorPtr<fidl::StringPtr> session_agents,
                   fidl::VectorPtr<fidl::StringPtr> startup_agents) override;

  void GetServicesForAgent(fidl::StringPtr url,
                           GetServicesForAgentCallback callback) override;

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
    void ConnectOrQueueServiceRequest(
        fidl::InterfaceRequest<Interface> request);

    fuchsia::modular::AgentControllerPtr controller;

    fuchsia::sys::ServiceProviderPtr services;
    // If an agent crashes, there is a period (~1 sec) where its |services|
    // interface is invalid before its controller is closed. During that
    // period, we should queue requests until we've restarted the agent.
    std::vector<DeferredInterfaceRequest> pending_service_requests;

    modular::RateLimitedRetry restart;
  };

  using ServiceProviderInitializer = std::function<void(
      const std::string& url, component::ServiceNamespace* agent_host)>;
  // A ServiceProviderInitializer that adds standard agent services, including
  // attributed context and suggestion service entry points. Returns the names
  // of the services added.
  fidl::VectorPtr<fidl::StringPtr> AddStandardServices(
      const std::string& url, component::ServiceNamespace* agent_host);

  // Starts an app in the parent environment, with full access to environment
  // services.
  component::Services StartTrustedApp(const std::string& url);

  void StartAgent(const std::string& url);

  void StartActionLog(fuchsia::modular::SuggestionEngine* suggestion_engine);
  void StartSessionAgent(const std::string& url);

  component::StartupContext* context_;  // Not owned.

  fuchsia::modular::ContextEnginePtr context_engine_;
  component::Services suggestion_services_;
  fuchsia::modular::SuggestionEnginePtr suggestion_engine_;

  std::map<std::string, SessionAgentData> session_agents_;

  fidl::BindingSet<fuchsia::modular::IntelligenceServices,
                   std::unique_ptr<fuchsia::modular::IntelligenceServices>>
      intelligence_services_bindings_;

  fidl::InterfacePtr<fuchsia::modular::ComponentContext> component_context_;
  fidl::InterfacePtr<fuchsia::modular::StoryProvider> story_provider_;
  fidl::InterfacePtr<fuchsia::modular::FocusProvider> focus_provider_;
  fidl::InterfacePtr<fuchsia::modular::PuppetMaster> puppet_master_;
  fidl::InterfacePtr<fuchsia::modular::VisibleStoriesProvider>
      visible_stories_provider_;

  // Framework fuchsia::modular::Agent controllers. Hanging onto these tells the
  // Framework we want the Agents to keep running.
  std::vector<fuchsia::modular::AgentControllerPtr> agent_controllers_;

  // ServiceNamespace(s) backing the services provided to these agents via its
  // namespace.
  std::deque<component::ServiceNamespace> agent_namespaces_;
};

}  // namespace modular

#endif  // PERIDOT_BIN_USER_RUNNER_USER_INTELLIGENCE_PROVIDER_IMPL_H_
