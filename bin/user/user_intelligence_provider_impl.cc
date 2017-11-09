// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user/user_intelligence_provider_impl.h"

#include "lib/action_log/fidl/factory.fidl.h"
#include "lib/app/cpp/connect.h"
#include "lib/bluetooth/fidl/low_energy.fidl.h"
#include "lib/cobalt/fidl/cobalt.fidl.h"
#include "lib/context/fidl/debug.fidl.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/network/fidl/network_service.fidl.h"
#include "lib/resolver/fidl/resolver.fidl.h"
#include "lib/suggestion/fidl/debug.fidl.h"
#include "lib/user_intelligence/fidl/scope.fidl.h"
#include "peridot/bin/acquirers/story_info/initializer.fidl.h"
#include "peridot/bin/user/intelligence_services_impl.h"

namespace maxwell {

namespace {

constexpr modular::RateLimitedRetry::Threshold kKronkRetryLimit = {
    3, fxl::TimeDelta::FromSeconds(8)};

// Calls Duplicate() on an InterfacePtr<> and returns the newly bound
// InterfaceHandle<>.
template <class T>
fidl::InterfaceHandle<T> Duplicate(const fidl::InterfacePtr<T>& ptr) {
  fidl::InterfaceHandle<T> handle;
  ptr->Duplicate(handle.NewRequest());
  return handle;
}

modular::AgentControllerPtr StartStoryInfoAgent(
    modular::ComponentContext* component_context,
    fidl::InterfaceHandle<modular::StoryProvider> story_provider,
    fidl::InterfaceHandle<modular::FocusProvider> focus_provider,
    fidl::InterfaceHandle<modular::VisibleStoriesProvider>
        visible_stories_provider) {
  app::ServiceProviderPtr agent_services;
  modular::AgentControllerPtr controller;
  component_context->ConnectToAgent("acquirers/story_info_main",
                                    agent_services.NewRequest(),
                                    controller.NewRequest());

  auto initializer =
      app::ConnectToService<StoryInfoInitializer>(agent_services.get());
  initializer->Initialize(std::move(story_provider), std::move(focus_provider),
                          std::move(visible_stories_provider));

  return controller;
}

}  // namespace

UserIntelligenceProviderImpl::UserIntelligenceProviderImpl(
    app::ApplicationContext* app_context,
    const Config& config,
    fidl::InterfaceHandle<modular::ComponentContext> component_context_handle,
    fidl::InterfaceHandle<modular::StoryProvider> story_provider_handle,
    fidl::InterfaceHandle<modular::FocusProvider> focus_provider_handle,
    fidl::InterfaceHandle<modular::VisibleStoriesProvider>
        visible_stories_provider_handle)
    : app_context_(app_context),
      kronk_restart_(kKronkRetryLimit),
      agent_launcher_(app_context_->environment().get()) {
  component_context_.Bind(std::move(component_context_handle));
  auto story_provider =
      modular::StoryProviderPtr::Create(std::move(story_provider_handle));
  auto focus_provider =
      modular::FocusProviderPtr::Create(std::move(focus_provider_handle));
  visible_stories_provider_.Bind(std::move(visible_stories_provider_handle));

  // Start dependent processes. We get some component-scope services from
  // these processes.
  context_services_ = StartTrustedApp("context_engine");
  context_engine_ =
      app::ConnectToService<maxwell::ContextEngine>(context_services_.get());
  suggestion_services_ = StartTrustedApp("suggestion_engine");
  suggestion_engine_ = app::ConnectToService<maxwell::SuggestionEngine>(
      suggestion_services_.get());

  // Generate a ContextWriter to pass to the SuggestionEngine.
  fidl::InterfaceHandle<ContextWriter> context_writer;
  auto scope = ComponentScope::New();
  scope->set_global_scope(GlobalScope::New());
  context_engine_->GetWriter(std::move(scope), context_writer.NewRequest());

  suggestion_engine_->Initialize(Duplicate(story_provider),
                                 Duplicate(focus_provider),
                                 std::move(context_writer));

  if (!config.kronk.empty()) {
    kronk_url_ = config.kronk;
    StartKronk();
  }

  StartActionLog(suggestion_engine_.get());

  for (const auto& agent : config.startup_agents) {
    StartAgent(agent);
  }

  if (config.mi_dashboard) {
    StartAgent(
        "agents/mi_dashboard.dartx",
        [=](const std::string& url, app::ServiceNamespace* agent_host) {
          AddStandardServices(url, agent_host);
          agent_host->AddService<maxwell::ContextDebug>(
              [=](fidl::InterfaceRequest<maxwell::ContextDebug> request) {
                context_engine_->GetContextDebug(std::move(request));
              });
          agent_host->AddService<maxwell::SuggestionDebug>(
              [=](fidl::InterfaceRequest<maxwell::SuggestionDebug> request) {
                app::ConnectToService(suggestion_services_.get(),
                                      std::move(request));
              });
          agent_host->AddService<maxwell::UserActionLog>(
              [this](fidl::InterfaceRequest<maxwell::UserActionLog> request) {
                user_action_log_->Duplicate(std::move(request));
              });
        });
  }

  // Start privileged local Framework-style Agents.
  {
    auto controller = StartStoryInfoAgent(
        component_context_.get(), Duplicate(story_provider),
        Duplicate(focus_provider), Duplicate(visible_stories_provider_));
    agent_controllers_.push_back(std::move(controller));
  }
}

void UserIntelligenceProviderImpl::GetComponentIntelligenceServices(
    ComponentScopePtr scope,
    fidl::InterfaceRequest<IntelligenceServices> request) {
  intelligence_services_bindings_.AddBinding(
      std::make_unique<IntelligenceServicesImpl>(
          std::move(scope), context_engine_.get(), suggestion_engine_.get(),
          user_action_log_.get()),
      std::move(request));
}

void UserIntelligenceProviderImpl::GetSuggestionProvider(
    fidl::InterfaceRequest<SuggestionProvider> request) {
  app::ConnectToService(suggestion_services_.get(), std::move(request));
}

void UserIntelligenceProviderImpl::GetResolver(
    fidl::InterfaceRequest<resolver::Resolver> request) {
  // TODO(thatguy): Remove this once the last instances of this are gone from
  // topaz.
}

app::ServiceProviderPtr UserIntelligenceProviderImpl::StartTrustedApp(
    const std::string& url) {
  app::ServiceProviderPtr services;
  auto launch_info = app::ApplicationLaunchInfo::New();
  launch_info->url = url;
  launch_info->services = services.NewRequest();
  app_context_->launcher()->CreateApplication(std::move(launch_info), NULL);
  return services;
}

void UserIntelligenceProviderImpl::StartActionLog(
    SuggestionEngine* suggestion_engine) {
  std::string url = "action_log";
  app::ServiceProviderPtr action_log_services = StartTrustedApp(url);
  maxwell::UserActionLogFactoryPtr action_log_factory =
      app::ConnectToService<maxwell::UserActionLogFactory>(
          action_log_services.get());
  maxwell::ProposalPublisherPtr proposal_publisher;
  suggestion_engine->RegisterProposalPublisher(
      url, fidl::GetProxy(&proposal_publisher));
  action_log_factory->GetUserActionLog(std::move(proposal_publisher),
                                       fidl::GetProxy(&user_action_log_));
}

void UserIntelligenceProviderImpl::StartKronk() {
  component_context_->ConnectToAgent(kronk_url_,
                                     kronk_services_.NewRequest(),
                                     kronk_controller_.NewRequest());
  auto kronk_stt = app::ConnectToService<SpeechToText>(kronk_services_.get());
  suggestion_engine_->SetSpeechToText(kronk_stt.PassInterfaceHandle());
  kronk_services_.set_connection_error_handler([=] {
    if (kronk_restart_.ShouldRetry()) {
      FXL_LOG(INFO) << "Restarting Kronk...";
      StartKronk();
    } else {
      FXL_LOG(WARNING) << "Kronk crashed more than " << kKronkRetryLimit.count
                       << " times in " << kKronkRetryLimit.period.ToSecondsF()
                       << " seconds. Speech capture disabled.";
    }
  });
}

void UserIntelligenceProviderImpl::AddStandardServices(
    const std::string& url,
    app::ServiceNamespace* agent_host) {
  auto agent_info = ComponentScope::New();
  auto agent_scope = AgentScope::New();
  agent_scope->url = url;
  agent_info->set_agent_scope(std::move(agent_scope));

  agent_host->AddService<cobalt::CobaltEncoderFactory>(
      [this,
       url](fidl::InterfaceRequest<cobalt::CobaltEncoderFactory> request) {
        app_context_->ConnectToEnvironmentService(std::move(request));
      });
  agent_host->AddService<maxwell::ContextWriter>(fxl::MakeCopyable(
      [this, client_info = agent_info.Clone(),
       url](fidl::InterfaceRequest<maxwell::ContextWriter> request) {
        context_engine_->GetWriter(client_info.Clone(), std::move(request));
      }));
  agent_host->AddService<maxwell::ContextReader>(fxl::MakeCopyable(
      [this, client_info = agent_info.Clone(),
       url](fidl::InterfaceRequest<maxwell::ContextReader> request) {
        context_engine_->GetReader(client_info.Clone(), std::move(request));
      }));
  agent_host->AddService<maxwell::IntelligenceServices>(fxl::MakeCopyable(
      [this, client_info = agent_info.Clone(),
       url](fidl::InterfaceRequest<maxwell::IntelligenceServices> request) {
        this->GetComponentIntelligenceServices(client_info.Clone(),
                                               std::move(request));
      }));
  agent_host->AddService<maxwell::ProposalPublisher>(
      [this, url](fidl::InterfaceRequest<maxwell::ProposalPublisher> request) {
        suggestion_engine_->RegisterProposalPublisher(url, std::move(request));
      });

  agent_host->AddService<modular::VisibleStoriesProvider>(
      [this](fidl::InterfaceRequest<modular::VisibleStoriesProvider> request) {
        visible_stories_provider_->Duplicate(std::move(request));
      });

  agent_host->AddService<network::NetworkService>(
      [this](fidl::InterfaceRequest<network::NetworkService> request) {
        app_context_->ConnectToEnvironmentService(std::move(request));
      });
  agent_host->AddService<bluetooth::low_energy::Central>(
      [this](fidl::InterfaceRequest<bluetooth::low_energy::Central> request) {
        app_context_->ConnectToEnvironmentService(std::move(request));
      });
  agent_host->AddService<resolver::Resolver>(std::bind(
      &UserIntelligenceProviderImpl::GetResolver, this, std::placeholders::_1));
}

app::ServiceProviderPtr UserIntelligenceProviderImpl::StartAgent(
    const std::string& url) {
  return StartAgent(
      url, std::bind(&UserIntelligenceProviderImpl::AddStandardServices, this,
                     std::placeholders::_1, std::placeholders::_2));
}

app::ServiceProviderPtr UserIntelligenceProviderImpl::StartAgent(
    const std::string& url,
    ServiceProviderInitializer services) {
  auto agent_host = std::make_unique<maxwell::ApplicationEnvironmentHostImpl>(
      app_context_->environment().get());

  services(url, agent_host.get());

  return agent_launcher_.StartAgent(url, std::move(agent_host));
}

//////////////////////////////////////////////////////////////////////////////

UserIntelligenceProviderFactoryImpl::UserIntelligenceProviderFactoryImpl(
    app::ApplicationContext* app_context,
    const Config& config)
    : app_context_(app_context), config_(config) {}

void UserIntelligenceProviderFactoryImpl::GetUserIntelligenceProvider(
    fidl::InterfaceHandle<modular::ComponentContext> component_context,
    fidl::InterfaceHandle<modular::StoryProvider> story_provider,
    fidl::InterfaceHandle<modular::FocusProvider> focus_provider,
    fidl::InterfaceHandle<modular::VisibleStoriesProvider>
        visible_stories_provider,
    fidl::InterfaceRequest<UserIntelligenceProvider>
        user_intelligence_provider_request) {
  // Fail if someone has already used this Factory to create an instance of
  // UserIntelligenceProvider.
  FXL_CHECK(!impl_);
  impl_.reset(new UserIntelligenceProviderImpl(
      app_context_, config_, std::move(component_context),
      std::move(story_provider), std::move(focus_provider),
      std::move(visible_stories_provider)));
  binding_.reset(new fidl::Binding<UserIntelligenceProvider>(impl_.get()));
  binding_->Bind(std::move(user_intelligence_provider_request));
}

}  // namespace maxwell
