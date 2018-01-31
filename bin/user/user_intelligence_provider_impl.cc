// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user/user_intelligence_provider_impl.h"

#include "lib/action_log/fidl/factory.fidl.h"
#include "lib/app/cpp/connect.h"
#include "lib/app/fidl/application_launcher.fidl.h"
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

constexpr char kMIDashboardUrl[] = "agents/mi_dashboard";

namespace {

constexpr modular::RateLimitedRetry::Threshold kKronkRetryLimit = {
    3, fxl::TimeDelta::FromSeconds(45)};

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
    fidl::InterfaceHandle<maxwell::ContextEngine> context_engine_handle,
    fidl::InterfaceHandle<modular::StoryProvider> story_provider_handle,
    fidl::InterfaceHandle<modular::FocusProvider> focus_provider_handle,
    fidl::InterfaceHandle<modular::VisibleStoriesProvider>
        visible_stories_provider_handle)
    : app_context_(app_context),
      config_(config),
      kronk_restart_(kKronkRetryLimit) {
  context_engine_.Bind(std::move(context_engine_handle));
  story_provider_.Bind(std::move(story_provider_handle));
  focus_provider_.Bind(std::move(focus_provider_handle));
  visible_stories_provider_.Bind(std::move(visible_stories_provider_handle));

  // Start dependent processes. We get some component-scope services from
  // these processes.
  suggestion_services_ = StartTrustedApp("suggestion_engine");
  suggestion_engine_ =
      suggestion_services_.ConnectToService<maxwell::SuggestionEngine>();

  // Generate a ContextWriter to pass to the SuggestionEngine.
  fidl::InterfaceHandle<ContextWriter> context_writer;
  auto scope = ComponentScope::New();
  scope->set_global_scope(GlobalScope::New());
  context_engine_->GetWriter(std::move(scope), context_writer.NewRequest());

  suggestion_engine_->Initialize(Duplicate(story_provider_),
                                 Duplicate(focus_provider_),
                                 std::move(context_writer));

  StartActionLog(suggestion_engine_.get());
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
  suggestion_services_.ConnectToService(std::move(request));
}

void UserIntelligenceProviderImpl::GetSpeechToText(
    fidl::InterfaceRequest<speech::SpeechToText> request) {
  if (kronk_services_) {
    app::ConnectToService(kronk_services_.get(), std::move(request));
  } else {
    FXL_LOG(WARNING) << "No speech-to-text agent loaded";
  }
}

void UserIntelligenceProviderImpl::GetResolver(
    fidl::InterfaceRequest<resolver::Resolver> request) {
  // TODO(thatguy): Remove this once the last instances of this are gone from
  // topaz.
}

void UserIntelligenceProviderImpl::StartAgents(
    fidl::InterfaceHandle<modular::ComponentContext> component_context_handle) {
  component_context_.Bind(std::move(component_context_handle));

  if (!config_.kronk.empty()) {
    // TODO(rosswang): We are in the process of switching to in-tree Kronk.
    // (This comment is left at the request of the security team.)
    kronk_url_ = config_.kronk;
    StartKronk();
  }

  if (config_.mi_dashboard) {
   StartAgent(kMIDashboardUrl);
  }

  for (const auto& agent : config_.startup_agents) {
    StartAgent(agent);
 }

  auto controller = StartStoryInfoAgent(
      component_context_.get(), Duplicate(story_provider_),
      Duplicate(focus_provider_), Duplicate(visible_stories_provider_));
  agent_controllers_.push_back(std::move(controller));
}

void UserIntelligenceProviderImpl::GetServicesForAgent(
    const fidl::String& url,
    const GetServicesForAgentCallback& callback) {
  auto service_list = app::ServiceList::New();
  agent_namespaces_.emplace_back(service_list->provider.NewRequest());
  service_list->names = AddStandardServices(url, &agent_namespaces_.back());
  callback(std::move(service_list));
}

app::Services UserIntelligenceProviderImpl::StartTrustedApp(
    const std::string& url) {
  app::Services services;
  auto launch_info = app::ApplicationLaunchInfo::New();
  launch_info->url = url;
  launch_info->service_request = services.NewRequest();
  app_context_->launcher()->CreateApplication(std::move(launch_info), NULL);
  return services;
}

void UserIntelligenceProviderImpl::StartAgent(const std::string& url) {
  modular::AgentControllerPtr controller;
  app::ServiceProviderPtr services;
  component_context_->ConnectToAgent(url, services.NewRequest(),
                                     controller.NewRequest());
  agent_controllers_.push_back(std::move(controller));
}

void UserIntelligenceProviderImpl::StartActionLog(
    SuggestionEngine* suggestion_engine) {
  std::string url = "action_log";
  app::Services action_log_services = StartTrustedApp(url);
  maxwell::UserActionLogFactoryPtr action_log_factory =
      action_log_services.ConnectToService<maxwell::UserActionLogFactory>();
  maxwell::ProposalPublisherPtr proposal_publisher;
  suggestion_engine->RegisterProposalPublisher(
      url, proposal_publisher.NewRequest());
  action_log_factory->GetUserActionLog(std::move(proposal_publisher),
                                       user_action_log_.NewRequest());
}

void UserIntelligenceProviderImpl::StartKronk() {
  component_context_->ConnectToAgent(kronk_url_, kronk_services_.NewRequest(),
                                     kronk_controller_.NewRequest());
  // Agent runner closes the agent controller connection when the agent
  // terminates. We restart the agent (up to a limit) when we notice this.
  //
  // NOTE(rosswang,mesch): Although the interface we're actually interested in
  // is kronk_services_, we still need to put the restart handler on the
  // controller. When the agent crashes, kronk_services_ often gets closed quite
  // a bit earlier (~1 second) than the agent runner notices via the application
  // controller (which it must use as opposed to any interface on the agent
  // itself since the agent is not required to implement any interfaces itself,
  // even though it is recommended that it does). If we try to restart the agent
  // at that time, the agent runner would attempt to simply send the connection
  // request to the crashed agent instance and not relaunch the agent.
  kronk_controller_.set_error_handler([this] {
    kronk_services_.Unbind();
    kronk_controller_.Unbind();

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

fidl::Array<fidl::String> UserIntelligenceProviderImpl::AddStandardServices(
    const std::string& url,
    app::ServiceNamespace* agent_host) {
  auto agent_info = ComponentScope::New();
  auto agent_scope = AgentScope::New();
  agent_scope->url = url;
  agent_info->set_agent_scope(std::move(agent_scope));
  fidl::Array<fidl::String> service_names;

  service_names.push_back(maxwell::ContextWriter::Name_);
  agent_host->AddService<maxwell::ContextWriter>(fxl::MakeCopyable(
      [this, client_info = agent_info.Clone(),
       url](fidl::InterfaceRequest<maxwell::ContextWriter> request) {
        context_engine_->GetWriter(client_info.Clone(), std::move(request));
      }));

  service_names.push_back(maxwell::ContextReader::Name_);
  agent_host->AddService<maxwell::ContextReader>(fxl::MakeCopyable(
      [this, client_info = agent_info.Clone(),
       url](fidl::InterfaceRequest<maxwell::ContextReader> request) {
        context_engine_->GetReader(client_info.Clone(), std::move(request));
      }));

  service_names.push_back(maxwell::IntelligenceServices::Name_);
  agent_host->AddService<maxwell::IntelligenceServices>(fxl::MakeCopyable(
      [this, client_info = agent_info.Clone(),
       url](fidl::InterfaceRequest<maxwell::IntelligenceServices> request) {
        this->GetComponentIntelligenceServices(client_info.Clone(),
                                               std::move(request));
      }));

  service_names.push_back(maxwell::ProposalPublisher::Name_);
  agent_host->AddService<maxwell::ProposalPublisher>(
      [this, url](fidl::InterfaceRequest<maxwell::ProposalPublisher> request) {
        suggestion_engine_->RegisterProposalPublisher(url, std::move(request));
      });

  service_names.push_back(modular::VisibleStoriesProvider::Name_);
  agent_host->AddService<modular::VisibleStoriesProvider>(
      [this](fidl::InterfaceRequest<modular::VisibleStoriesProvider> request) {
        visible_stories_provider_->Duplicate(std::move(request));
      });

  service_names.push_back(resolver::Resolver::Name_);
  agent_host->AddService<resolver::Resolver>(std::bind(
      &UserIntelligenceProviderImpl::GetResolver, this, std::placeholders::_1));

  if (url == kMIDashboardUrl) {
    service_names.push_back(maxwell::ContextDebug::Name_);
    agent_host->AddService<maxwell::ContextDebug>(
        [this](fidl::InterfaceRequest<maxwell::ContextDebug> request) {
          context_engine_->GetContextDebug(std::move(request));
        });

    service_names.push_back(maxwell::SuggestionDebug::Name_);
    agent_host->AddService<maxwell::SuggestionDebug>(
        [this](fidl::InterfaceRequest<maxwell::SuggestionDebug> request) {
          suggestion_services_.ConnectToService(std::move(request));
        });

    service_names.push_back(maxwell::UserActionLog::Name_);
    agent_host->AddService<maxwell::UserActionLog>(
        [this](fidl::InterfaceRequest<maxwell::UserActionLog> request) {
          user_action_log_->Duplicate(std::move(request));
        });
  }

  return service_names;
}

//////////////////////////////////////////////////////////////////////////////

UserIntelligenceProviderFactoryImpl::UserIntelligenceProviderFactoryImpl(
    app::ApplicationContext* app_context,
    const Config& config)
    : app_context_(app_context), config_(config) {}

void UserIntelligenceProviderFactoryImpl::GetUserIntelligenceProvider(
    fidl::InterfaceHandle<maxwell::ContextEngine> context_engine,
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
      app_context_, config_,
      std::move(context_engine), std::move(story_provider),
      std::move(focus_provider), std::move(visible_stories_provider)));
  binding_.reset(new fidl::Binding<UserIntelligenceProvider>(impl_.get()));
  binding_->Bind(std::move(user_intelligence_provider_request));
}

}  // namespace maxwell
