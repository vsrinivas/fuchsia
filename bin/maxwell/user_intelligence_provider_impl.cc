// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/maxwell/user_intelligence_provider_impl.h"

#include <fuchsia/bluetooth/le/cpp/fidl.h>
#include <fuchsia/cobalt/cpp/fidl.h>
#include <fuchsia/maxwell/internal/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/component/cpp/connect.h>
#include <lib/fxl/files/file.h>
#include <lib/fxl/functional/make_copyable.h>

#include "peridot/bin/maxwell/intelligence_services_impl.h"

namespace maxwell {

namespace {

constexpr char kMIDashboardUrl[] = "mi_dashboard";
constexpr char kUsageLogUrl[] = "usage_log";
constexpr char kStoryInfoAgentUrl[] = "story_info";

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

fuchsia::modular::AgentControllerPtr StartStoryInfoAgent(
    fuchsia::modular::ComponentContext* component_context,
    fidl::InterfaceHandle<fuchsia::modular::StoryProvider> story_provider,
    fidl::InterfaceHandle<fuchsia::modular::FocusProvider> focus_provider,
    fidl::InterfaceHandle<fuchsia::modular::VisibleStoriesProvider>
        visible_stories_provider) {
  fuchsia::sys::ServiceProviderPtr agent_services;
  fuchsia::modular::AgentControllerPtr controller;
  component_context->ConnectToAgent(
      kStoryInfoAgentUrl, agent_services.NewRequest(), controller.NewRequest());

  using fuchsia::maxwell::internal::StoryInfoInitializer;
  auto initializer = component::ConnectToService<StoryInfoInitializer>(
      agent_services.get());
  initializer->Initialize(std::move(story_provider), std::move(focus_provider),
                          std::move(visible_stories_provider));

  return controller;
}

fuchsia::modular::ComponentScope CloneScope(
    const fuchsia::modular::ComponentScope& scope) {
  fuchsia::modular::ComponentScope result;
  fidl::Clone(scope, &result);
  return result;
}

}  // namespace

UserIntelligenceProviderImpl::UserIntelligenceProviderImpl(
    component::StartupContext* context, const Config& config,
    fidl::InterfaceHandle<fuchsia::modular::ContextEngine>
        context_engine_handle,
    fidl::InterfaceHandle<fuchsia::modular::StoryProvider>
        story_provider_handle,
    fidl::InterfaceHandle<fuchsia::modular::FocusProvider>
        focus_provider_handle,
    fidl::InterfaceHandle<fuchsia::modular::VisibleStoriesProvider>
        visible_stories_provider_handle)
    : context_(context), config_(config), kronk_restart_(kKronkRetryLimit) {
  context_engine_.Bind(std::move(context_engine_handle));
  story_provider_.Bind(std::move(story_provider_handle));
  focus_provider_.Bind(std::move(focus_provider_handle));
  visible_stories_provider_.Bind(std::move(visible_stories_provider_handle));

  // Start dependent processes. We get some component-scope services from
  // these processes.
  suggestion_services_ = StartTrustedApp("suggestion_engine");
  suggestion_engine_ =
      suggestion_services_
          .ConnectToService<fuchsia::modular::SuggestionEngine>();

  // Generate a fuchsia::modular::ContextWriter and
  // fuchsia::modular::ContextReader to pass to the
  // fuchsia::modular::SuggestionEngine.
  fidl::InterfaceHandle<fuchsia::modular::ContextReader> context_reader;
  fidl::InterfaceHandle<fuchsia::modular::ContextWriter> context_writer;
  fuchsia::modular::ComponentScope scope1;
  scope1.set_global_scope(fuchsia::modular::GlobalScope());
  fuchsia::modular::ComponentScope scope2;
  fidl::Clone(scope1, &scope2);
  context_engine_->GetWriter(std::move(scope1), context_writer.NewRequest());
  context_engine_->GetReader(std::move(scope2), context_reader.NewRequest());

  suggestion_engine_->Initialize(
      Duplicate(story_provider_), Duplicate(focus_provider_),
      std::move(context_writer), std::move(context_reader));

  StartActionLog(suggestion_engine_.get());
}

void UserIntelligenceProviderImpl::GetComponentIntelligenceServices(
    fuchsia::modular::ComponentScope scope,
    fidl::InterfaceRequest<fuchsia::modular::IntelligenceServices> request) {
  intelligence_services_bindings_.AddBinding(
      std::make_unique<IntelligenceServicesImpl>(
          std::move(scope), context_engine_.get(), suggestion_engine_.get(),
          user_action_log_.get()),
      std::move(request));
}

void UserIntelligenceProviderImpl::GetSuggestionProvider(
    fidl::InterfaceRequest<fuchsia::modular::SuggestionProvider> request) {
  suggestion_services_.ConnectToService(std::move(request));
}

void UserIntelligenceProviderImpl::GetSpeechToText(
    fidl::InterfaceRequest<fuchsia::speech::SpeechToText> request) {
  if (kronk_services_) {
    component::ConnectToService(kronk_services_.get(), std::move(request));
  } else {
    FXL_LOG(WARNING) << "No speech-to-text agent loaded";
  }
}

void UserIntelligenceProviderImpl::StartAgents(
    fidl::InterfaceHandle<fuchsia::modular::ComponentContext>
        component_context_handle) {
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
    fidl::StringPtr url, GetServicesForAgentCallback callback) {
  fuchsia::sys::ServiceList service_list;
  agent_namespaces_.emplace_back(service_list.provider.NewRequest());
  service_list.names = AddStandardServices(url, &agent_namespaces_.back());
  callback(std::move(service_list));
}

component::Services UserIntelligenceProviderImpl::StartTrustedApp(
    const std::string& url) {
  component::Services services;
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = url;
  launch_info.directory_request = services.NewRequest();
  context_->launcher()->CreateComponent(std::move(launch_info), NULL);
  return services;
}

void UserIntelligenceProviderImpl::StartAgent(const std::string& url) {
  fuchsia::modular::AgentControllerPtr controller;
  fuchsia::sys::ServiceProviderPtr services;
  component_context_->ConnectToAgent(url, services.NewRequest(),
                                     controller.NewRequest());
  agent_controllers_.push_back(std::move(controller));
}

void UserIntelligenceProviderImpl::StartActionLog(
    fuchsia::modular::SuggestionEngine* suggestion_engine) {
  std::string url = "action_log";
  component::Services action_log_services = StartTrustedApp(url);
  fuchsia::modular::UserActionLogFactoryPtr action_log_factory =
      action_log_services
          .ConnectToService<fuchsia::modular::UserActionLogFactory>();
  fuchsia::modular::ProposalPublisherPtr proposal_publisher;
  suggestion_engine->RegisterProposalPublisher(url,
                                               proposal_publisher.NewRequest());
  action_log_factory->GetUserActionLog(std::move(proposal_publisher),
                                       user_action_log_.NewRequest());
}

void UserIntelligenceProviderImpl::StartKronk() {
  component_context_->ConnectToAgent(kronk_url_, kronk_services_.NewRequest(),
                                     kronk_controller_.NewRequest());

  fuchsia::modular::KronkInitializerPtr initializer;
  component::ConnectToService(kronk_services_.get(),
                                 initializer.NewRequest());
  initializer->Initialize(Duplicate(focus_provider_));

  // fuchsia::modular::Agent runner closes the agent controller connection when
  // the agent terminates. We restart the agent (up to a limit) when we notice
  // this.
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

fidl::VectorPtr<fidl::StringPtr>
UserIntelligenceProviderImpl::AddStandardServices(
    const std::string& url, component::ServiceNamespace* agent_host) {
  fuchsia::modular::ComponentScope agent_info;
  fuchsia::modular::AgentScope agent_scope;
  agent_scope.url = url;
  agent_info.set_agent_scope(std::move(agent_scope));
  fidl::VectorPtr<fidl::StringPtr> service_names;

  service_names.push_back(fuchsia::modular::ContextWriter::Name_);
  agent_host->AddService<fuchsia::modular::ContextWriter>(fxl::MakeCopyable(
      [this, client_info = CloneScope(agent_info),
       url](fidl::InterfaceRequest<fuchsia::modular::ContextWriter> request) {
        context_engine_->GetWriter(CloneScope(client_info), std::move(request));
      }));

  service_names.push_back(fuchsia::modular::ContextReader::Name_);
  agent_host->AddService<fuchsia::modular::ContextReader>(fxl::MakeCopyable(
      [this, client_info = CloneScope(agent_info),
       url](fidl::InterfaceRequest<fuchsia::modular::ContextReader> request) {
        context_engine_->GetReader(CloneScope(client_info), std::move(request));
      }));

  service_names.push_back(fuchsia::modular::IntelligenceServices::Name_);
  agent_host->AddService<fuchsia::modular::IntelligenceServices>(
      fxl::MakeCopyable(
          [this, client_info = CloneScope(agent_info),
           url](fidl::InterfaceRequest<fuchsia::modular::IntelligenceServices>
                    request) {
            this->GetComponentIntelligenceServices(CloneScope(client_info),
                                                   std::move(request));
          }));

  service_names.push_back(fuchsia::modular::ProposalPublisher::Name_);
  agent_host->AddService<fuchsia::modular::ProposalPublisher>(
      [this, url](
          fidl::InterfaceRequest<fuchsia::modular::ProposalPublisher> request) {
        suggestion_engine_->RegisterProposalPublisher(url, std::move(request));
      });

  service_names.push_back(fuchsia::modular::VisibleStoriesProvider::Name_);
  agent_host->AddService<fuchsia::modular::VisibleStoriesProvider>(
      [this](fidl::InterfaceRequest<fuchsia::modular::VisibleStoriesProvider>
                 request) {
        visible_stories_provider_->Duplicate(std::move(request));
      });

  if (url == kMIDashboardUrl || url == kUsageLogUrl) {
    service_names.push_back(fuchsia::modular::ContextDebug::Name_);
    agent_host->AddService<fuchsia::modular::ContextDebug>(
        [this](fidl::InterfaceRequest<fuchsia::modular::ContextDebug> request) {
          context_engine_->GetContextDebug(std::move(request));
        });

    service_names.push_back(fuchsia::modular::SuggestionDebug::Name_);
    agent_host->AddService<fuchsia::modular::SuggestionDebug>(
        [this](
            fidl::InterfaceRequest<fuchsia::modular::SuggestionDebug> request) {
          suggestion_services_.ConnectToService(std::move(request));
        });

    service_names.push_back(fuchsia::modular::UserActionLog::Name_);
    agent_host->AddService<fuchsia::modular::UserActionLog>(
        [this](
            fidl::InterfaceRequest<fuchsia::modular::UserActionLog> request) {
          user_action_log_->Duplicate(std::move(request));
        });
  }

  return service_names;
}

//////////////////////////////////////////////////////////////////////////////

UserIntelligenceProviderFactoryImpl::UserIntelligenceProviderFactoryImpl(
    component::StartupContext* context, const Config& config)
    : context_(context), config_(config) {}

void UserIntelligenceProviderFactoryImpl::GetUserIntelligenceProvider(
    fidl::InterfaceHandle<fuchsia::modular::ContextEngine> context_engine,
    fidl::InterfaceHandle<fuchsia::modular::StoryProvider> story_provider,
    fidl::InterfaceHandle<fuchsia::modular::FocusProvider> focus_provider,
    fidl::InterfaceHandle<fuchsia::modular::VisibleStoriesProvider>
        visible_stories_provider,
    fidl::InterfaceRequest<fuchsia::modular::UserIntelligenceProvider>
        user_intelligence_provider_request) {
  // Fail if someone has already used this Factory to create an instance of
  // fuchsia::modular::UserIntelligenceProvider.
  FXL_CHECK(!impl_);
  impl_.reset(new UserIntelligenceProviderImpl(
      context_, config_, std::move(context_engine), std::move(story_provider),
      std::move(focus_provider), std::move(visible_stories_provider)));
  binding_.reset(new fidl::Binding<fuchsia::modular::UserIntelligenceProvider>(
      impl_.get()));
  binding_->Bind(std::move(user_intelligence_provider_request));
}

}  // namespace maxwell
