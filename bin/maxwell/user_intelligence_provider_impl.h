// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_MAXWELL_USER_INTELLIGENCE_PROVIDER_IMPL_H_
#define PERIDOT_BIN_MAXWELL_USER_INTELLIGENCE_PROVIDER_IMPL_H_

#include <deque>

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/svc/cpp/services.h>

#include "peridot/bin/maxwell/agent_launcher.h"
#include "peridot/bin/maxwell/config.h"
#include "peridot/lib/util/rate_limited_retry.h"

namespace maxwell {

class UserIntelligenceProviderImpl
    : public fuchsia::modular::UserIntelligenceProvider {
 public:
  // |context| is not owned and must outlive this instance.
  UserIntelligenceProviderImpl(
      component::StartupContext* context, const Config& config,
      fidl::InterfaceHandle<fuchsia::modular::ContextEngine> context_engine,
      fidl::InterfaceHandle<fuchsia::modular::StoryProvider> story_provider,
      fidl::InterfaceHandle<fuchsia::modular::FocusProvider> focus_provider,
      fidl::InterfaceHandle<fuchsia::modular::VisibleStoriesProvider>
          visible_stories_provider,
      fidl::InterfaceHandle<fuchsia::modular::PuppetMaster> puppet_master);
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
                       component_context) override;

  void GetServicesForAgent(fidl::StringPtr url,
                           GetServicesForAgentCallback callback) override;

 private:
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
  void StartKronk();

  component::StartupContext* context_;  // Not owned.
  const Config config_;

  fuchsia::modular::ContextEnginePtr context_engine_;
  component::Services suggestion_services_;
  fuchsia::modular::SuggestionEnginePtr suggestion_engine_;
  fuchsia::modular::UserActionLogPtr user_action_log_;

  std::string kronk_url_;
  modular::RateLimitedRetry kronk_restart_;
  fuchsia::sys::ServiceProviderPtr kronk_services_;
  fuchsia::modular::AgentControllerPtr kronk_controller_;

  fidl::BindingSet<fuchsia::modular::IntelligenceServices,
                   std::unique_ptr<fuchsia::modular::IntelligenceServices>>
      intelligence_services_bindings_;

  fidl::InterfacePtr<fuchsia::modular::ComponentContext> component_context_;
  fidl::InterfacePtr<fuchsia::modular::StoryProvider> story_provider_;
  fidl::InterfacePtr<fuchsia::modular::FocusProvider> focus_provider_;
  fidl::InterfacePtr<fuchsia::modular::VisibleStoriesProvider>
      visible_stories_provider_;

  // Framework fuchsia::modular::Agent controllers. Hanging onto these tells the
  // Framework we want the Agents to keep running.
  std::vector<fuchsia::modular::AgentControllerPtr> agent_controllers_;

  // ServiceNamespace(s) backing the services provided to these agents via its
  // namespace.
  std::deque<component::ServiceNamespace> agent_namespaces_;
};

class UserIntelligenceProviderFactoryImpl
    : public fuchsia::modular::UserIntelligenceProviderFactory {
 public:
  // |context| is not owned and must outlive this instance.
  UserIntelligenceProviderFactoryImpl(component::StartupContext* context,
                                      const Config& config);
  ~UserIntelligenceProviderFactoryImpl() override = default;

  void GetUserIntelligenceProvider(
      fidl::InterfaceHandle<fuchsia::modular::ContextEngine> context_engine,
      fidl::InterfaceHandle<fuchsia::modular::StoryProvider> story_provider,
      fidl::InterfaceHandle<fuchsia::modular::FocusProvider> focus_provider,
      fidl::InterfaceHandle<fuchsia::modular::VisibleStoriesProvider>
          visible_stories_provider,
      fidl::InterfaceHandle<fuchsia::modular::PuppetMaster> puppet_master,
      fidl::InterfaceRequest<fuchsia::modular::UserIntelligenceProvider>
          user_intelligence_provider_request) override;

 private:
  component::StartupContext* context_;  // Not owned.
  const Config config_;

  // We expect a 1:1 relationship between instances of this Factory and
  // instances of fuchsia::modular::UserIntelligenceProvider.
  std::unique_ptr<UserIntelligenceProviderImpl> impl_;
  std::unique_ptr<fidl::Binding<fuchsia::modular::UserIntelligenceProvider>>
      binding_;
};

}  // namespace maxwell

#endif  // PERIDOT_BIN_MAXWELL_USER_INTELLIGENCE_PROVIDER_IMPL_H_
