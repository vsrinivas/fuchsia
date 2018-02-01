// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_USER_USER_INTELLIGENCE_PROVIDER_IMPL_H_
#define PERIDOT_BIN_USER_USER_INTELLIGENCE_PROVIDER_IMPL_H_

#include <deque>

#include "lib/action_log/fidl/user.fidl.h"
#include "lib/app/cpp/application_context.h"
#include "lib/context/fidl/context_engine.fidl.h"
#include "lib/resolver/fidl/resolver.fidl.h"
#include "lib/suggestion/fidl/suggestion_engine.fidl.h"
#include "lib/svc/cpp/services.h"
#include "lib/user_intelligence/fidl/scope.fidl.h"
#include "lib/user_intelligence/fidl/user_intelligence_provider.fidl.h"
#include "peridot/bin/user/agent_launcher.h"
#include "peridot/bin/user/config.h"
#include "peridot/lib/util/rate_limited_retry.h"

namespace maxwell {

class UserIntelligenceProviderImpl : public UserIntelligenceProvider {
 public:
  // |app_context| is not owned and must outlive this instance.
  UserIntelligenceProviderImpl(
      app::ApplicationContext* app_context,
      const Config& config,
      fidl::InterfaceHandle<maxwell::ContextEngine> context_engine,
      fidl::InterfaceHandle<modular::StoryProvider> story_provider,
      fidl::InterfaceHandle<modular::FocusProvider> focus_provider,
      fidl::InterfaceHandle<modular::VisibleStoriesProvider>
          visible_stories_provider);
  ~UserIntelligenceProviderImpl() override = default;

  void GetComponentIntelligenceServices(
      ComponentScopePtr scope,
      fidl::InterfaceRequest<IntelligenceServices> request) override;

  void GetSuggestionProvider(
      fidl::InterfaceRequest<SuggestionProvider> request) override;

  void GetSpeechToText(
      fidl::InterfaceRequest<speech::SpeechToText> request) override;

  void GetResolver(fidl::InterfaceRequest<resolver::Resolver> request) override;

  void StartAgents(fidl::InterfaceHandle<modular::ComponentContext>
                       component_context) override;

  void GetServicesForAgent(
      const fidl::String& url,
      const GetServicesForAgentCallback& callback) override;

 private:
  using ServiceProviderInitializer =
      std::function<void(const std::string& url,
                         app::ServiceNamespace* agent_host)>;
  // A ServiceProviderInitializer that adds standard agent services, including
  // attributed context and suggestion service entry points. Returns the names
  // of the services added.
  fidl::Array<fidl::String> AddStandardServices(
      const std::string& url,
      app::ServiceNamespace* agent_host);

  // Starts an app in the parent environment, with full access to environment
  // services.
  app::Services StartTrustedApp(const std::string& url);

  void StartAgent(const std::string& url);

  void StartActionLog(SuggestionEngine* suggestion_engine);
  void StartKronk();

  app::ApplicationContext* app_context_;  // Not owned.
  const Config config_;

  ContextEnginePtr context_engine_;
  app::Services suggestion_services_;
  SuggestionEnginePtr suggestion_engine_;
  UserActionLogPtr user_action_log_;

  std::string kronk_url_;
  modular::RateLimitedRetry kronk_restart_;
  app::ServiceProviderPtr kronk_services_;
  modular::AgentControllerPtr kronk_controller_;

  fidl::BindingSet<IntelligenceServices, std::unique_ptr<IntelligenceServices>>
      intelligence_services_bindings_;

  fidl::InterfacePtr<modular::ComponentContext> component_context_;
  fidl::InterfacePtr<modular::StoryProvider> story_provider_;
  fidl::InterfacePtr<modular::FocusProvider> focus_provider_;
  fidl::InterfacePtr<modular::VisibleStoriesProvider> visible_stories_provider_;

  // Framework Agent controllers. Hanging onto these tells the Framework we
  // want the Agents to keep running.
  std::vector<modular::AgentControllerPtr> agent_controllers_;

  // ServiceNamespace(s) backing the services provided to these agents via its
  // namespace.
  std::deque<app::ServiceNamespace> agent_namespaces_;
};

class UserIntelligenceProviderFactoryImpl
    : public UserIntelligenceProviderFactory {
 public:
  // |app_context| is not owned and must outlive this instance.
  UserIntelligenceProviderFactoryImpl(app::ApplicationContext* app_context,
                                      const Config& config);
  ~UserIntelligenceProviderFactoryImpl() override = default;

  void GetUserIntelligenceProvider(
      fidl::InterfaceHandle<maxwell::ContextEngine> context_engine,
      fidl::InterfaceHandle<modular::StoryProvider> story_provider,
      fidl::InterfaceHandle<modular::FocusProvider> focus_provider,
      fidl::InterfaceHandle<modular::VisibleStoriesProvider>
          visible_stories_provider,
      fidl::InterfaceRequest<UserIntelligenceProvider>
          user_intelligence_provider_request) override;

 private:
  app::ApplicationContext* app_context_;  // Not owned.
  const Config config_;

  // We expect a 1:1 relationship between instances of this Factory and
  // instances of UserIntelligenceProvider.
  std::unique_ptr<UserIntelligenceProviderImpl> impl_;
  std::unique_ptr<fidl::Binding<UserIntelligenceProvider>> binding_;
};

}  // namespace maxwell

#endif  // PERIDOT_BIN_USER_USER_INTELLIGENCE_PROVIDER_IMPL_H_
