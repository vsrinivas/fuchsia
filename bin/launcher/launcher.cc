// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "application/lib/app/application_context.h"
#include "application/lib/app/connect.h"
#include "apps/maxwell/services/context/context_engine.fidl.h"
#include "apps/maxwell/services/launcher/launcher.fidl.h"
#include "apps/maxwell/services/suggestion/suggestion_engine.fidl.h"
#include "apps/maxwell/src/launcher/agent_launcher.h"
#include "apps/network/services/network_service.fidl.h"
#include "lib/mtl/tasks/message_loop.h"

namespace {

// TODO(rosswang): determine if lifecycle controls are needed
class LauncherApp : public maxwell::Launcher {
 public:
  LauncherApp()
      : app_context_(app::ApplicationContext::CreateFromStartupInfo()),
        agent_launcher_(app_context_->environment().get()) {
    context_services_ =
        StartServiceProvider("file:///system/apps/context_engine");
    context_engine_ =
        app::ConnectToService<maxwell::ContextEngine>(context_services_.get());
    suggestion_services_ =
        StartServiceProvider("file:///system/apps/suggestion_engine");
    suggestion_engine_ = app::ConnectToService<maxwell::SuggestionEngine>(
        suggestion_services_.get());

    auto services = app_context_->outgoing_services();
    services->AddService<maxwell::Launcher>(
        [this](fidl::InterfaceRequest<maxwell::Launcher> request) {
          launcher_bindings_.AddBinding(this, std::move(request));
        });
    services->AddService<maxwell::SuggestionProvider>(
        [this](fidl::InterfaceRequest<maxwell::SuggestionProvider> request) {
          app::ConnectToService<maxwell::SuggestionProvider>(
              suggestion_services_.get(), std::move(request));
        });
    services->AddService<maxwell::ContextEngine>(
        [this](fidl::InterfaceRequest<maxwell::ContextEngine> request) {
          app::ConnectToService<maxwell::ContextEngine>(context_services_.get(),
                                                        std::move(request));
        });
  }

  void Initialize(fidl::InterfaceHandle<modular::StoryProvider> story_provider,
                  fidl::InterfaceHandle<modular::FocusController>
                      focus_controller) override {
    focus_controller_.Bind(std::move(focus_controller));

    fidl::InterfaceHandle<modular::FocusController> focus_controller_dup;
    auto focus_controller_request = focus_controller_dup.NewRequest();
    focus_controller_->Duplicate(std::move(focus_controller_request));

    suggestion_engine_->Initialize(std::move(story_provider),
                                   std::move(focus_controller_dup));

    // TODO(rosswang): Search the ComponentIndex and iterate through results.
    StartAgent("file:///system/apps/acquirers/focus");
    StartAgent("file:///system/apps/agents/bandsintown.dartx");
    StartAgent("file:///system/apps/agents/module_suggester");

    // This will error harmlessly if Kronk is not available.
    StartAgent("file:///system/apps/agents/kronk");
  }

  void RegisterAnonymousProposalPublisher(
      fidl::InterfaceRequest<maxwell::ProposalPublisher> proposal_publisher)
      override {
    suggestion_engine_->RegisterPublisher("unknown",
                                          std::move(proposal_publisher));
  }

 private:
  app::ServiceProviderPtr StartServiceProvider(const std::string& url) {
    app::ServiceProviderPtr services;
    auto launch_info = app::ApplicationLaunchInfo::New();
    launch_info->url = url;
    launch_info->services = services.NewRequest();
    app_context_->launcher()->CreateApplication(std::move(launch_info), NULL);
    return services;
  }

  void StartAgent(const std::string& url) {
    auto agent_host = std::make_unique<maxwell::ApplicationEnvironmentHostImpl>(
        app_context_->environment().get());

    agent_host->AddService<maxwell::ContextPublisher>(
        [this, url](fidl::InterfaceRequest<maxwell::ContextPublisher> request) {
          context_engine_->RegisterPublisher(url, std::move(request));
        });
    agent_host->AddService<maxwell::ContextPubSub>(
        [this, url](fidl::InterfaceRequest<maxwell::ContextPubSub> request) {
          context_engine_->RegisterPubSub(url, std::move(request));
        });
    agent_host->AddService<maxwell::ContextSubscriber>([this, url](
        fidl::InterfaceRequest<maxwell::ContextSubscriber> request) {
      context_engine_->RegisterSubscriber(url, std::move(request));
    });
    agent_host->AddService<maxwell::ProposalPublisher>([this, url](
        fidl::InterfaceRequest<maxwell::ProposalPublisher> request) {
      suggestion_engine_->RegisterPublisher(url, std::move(request));
    });

    agent_host->AddService<modular::FocusController>(
        [this](fidl::InterfaceRequest<modular::FocusController> request) {
          focus_controller_->Duplicate(std::move(request));
        });
    agent_host->AddService<network::NetworkService>(
        [this](fidl::InterfaceRequest<network::NetworkService> request) {
          app_context_->ConnectToEnvironmentService(std::move(request));
        });

    agent_launcher_.StartAgent(url, std::move(agent_host));
  }

  std::unique_ptr<app::ApplicationContext> app_context_;

  fidl::BindingSet<maxwell::Launcher> launcher_bindings_;

  app::ServiceProviderPtr context_services_;
  maxwell::ContextEnginePtr context_engine_;
  app::ServiceProviderPtr suggestion_services_;
  maxwell::SuggestionEnginePtr suggestion_engine_;

  maxwell::AgentLauncher agent_launcher_;

  fidl::InterfacePtr<modular::FocusController> focus_controller_;
};

}  // namespace

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  LauncherApp app;
  loop.Run();
  return 0;
}
