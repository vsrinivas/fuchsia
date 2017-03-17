// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "application/lib/app/application_context.h"
#include "apps/maxwell/services/context/context_engine.fidl.h"
#include "apps/maxwell/services/suggestion/suggestion_engine.fidl.h"
#include "apps/maxwell/services/user/user_intelligence_provider.fidl.h"
#include "apps/maxwell/src/user/agent_launcher.h"

namespace maxwell {

class UserIntelligenceProviderImpl : public UserIntelligenceProvider {
 public:
  // |app_context| is not owned and must outlive this instance.
  UserIntelligenceProviderImpl(
      app::ApplicationContext* app_context,
      fidl::InterfaceHandle<modular::ComponentContext> component_context,
      fidl::InterfaceHandle<modular::StoryProvider> story_provider,
      fidl::InterfaceHandle<modular::FocusProvider> focus_provider,
      fidl::InterfaceHandle<modular::VisibleStoriesProvider>
          visible_stories_provider);
  ~UserIntelligenceProviderImpl() override = default;

  void GetComponentIntelligenceServices(
      const fidl::String& story_id,
      const fidl::String& component_id,
      fidl::InterfaceRequest<IntelligenceServices> request) override;

  void GetSuggestionProvider(
      fidl::InterfaceRequest<SuggestionProvider> request) override;

 private:
  app::ServiceProviderPtr startServiceProviderApp(const std::string& url);
  void startAgent(const std::string& url);

  app::ApplicationContext* app_context_;  // Not owned.

  app::ServiceProviderPtr context_services_;
  ContextEnginePtr context_engine_;
  app::ServiceProviderPtr suggestion_services_;
  SuggestionEnginePtr suggestion_engine_;

  fidl::InterfacePtr<modular::FocusController> focus_controller_;

  AgentLauncher agent_launcher_;

  fidl::BindingSet<IntelligenceServices, std::unique_ptr<IntelligenceServices>>
      intelligence_services_bindings_;

  fidl::InterfacePtr<modular::VisibleStoriesProvider> visible_stories_provider_;
};

class UserIntelligenceProviderFactoryImpl
    : public UserIntelligenceProviderFactory {
 public:
  // |app_context| is not owned and must outlive this instance.
  UserIntelligenceProviderFactoryImpl(app::ApplicationContext* app_context);
  ~UserIntelligenceProviderFactoryImpl() override = default;

  void GetUserIntelligenceProvider(
      fidl::InterfaceHandle<modular::ComponentContext> component_context,
      fidl::InterfaceHandle<modular::StoryProvider> story_provider,
      fidl::InterfaceHandle<modular::FocusProvider> focus_provider,
      fidl::InterfaceHandle<modular::VisibleStoriesProvider>
          visible_stories_provider,
      fidl::InterfaceRequest<UserIntelligenceProvider>
          user_intelligence_provider_request) override;

 private:
  app::ApplicationContext* app_context_;  // Not owned.

  // We expect a 1:1 relationship between instances of this Factory and
  // instances of UserIntelligenceProvider.
  std::unique_ptr<UserIntelligenceProviderImpl> impl_;
  std::unique_ptr<fidl::Binding<UserIntelligenceProvider>> binding_;
};

}  // namespace maxwell
