// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/services/context/context_engine.fidl.h"
#include "apps/maxwell/services/launcher/launcher.fidl.h"
#include "apps/maxwell/services/suggestion/suggestion_engine.fidl.h"
#include "apps/maxwell/src/launcher/agent_launcher.h"
#include "apps/modular/lib/app/application_context.h"
#include "apps/modular/lib/app/connect.h"
#include "lib/mtl/tasks/message_loop.h"

namespace {

// TODO(rosswang): determine if lifecycle controls are needed
class LauncherApp : public maxwell::Launcher {
 public:
  LauncherApp()
      : app_context_(modular::ApplicationContext::CreateFromStartupInfo()),
        context_engine_(ConnectToService<maxwell::ContextEngine>(
            "file:///system/apps/context_engine")),
        agent_launcher_(app_context_->environment().get()) {
    suggestion_services_ =
        StartServiceProvider("file:///system/apps/suggestion_engine");
    suggestion_engine_ = modular::ConnectToService<maxwell::SuggestionEngine>(
        suggestion_services_.get());

    app_context_->outgoing_services()->AddService<maxwell::Launcher>(
        [this](fidl::InterfaceRequest<maxwell::Launcher> request) {
          launcher_bindings_.AddBinding(this, std::move(request));
        });
    app_context_->outgoing_services()->AddService<maxwell::SuggestionProvider>(
        [this](fidl::InterfaceRequest<maxwell::SuggestionProvider> request) {
          modular::ConnectToService<maxwell::SuggestionProvider>(
              suggestion_services_.get(), std::move(request));
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
    StartAgent("file:///system/apps/agents/module_suggester");
  }

 private:
  modular::ServiceProviderPtr StartServiceProvider(const std::string& url) {
    modular::ServiceProviderPtr services;
    auto launch_info = modular::ApplicationLaunchInfo::New();
    launch_info->url = url;
    launch_info->services = services.NewRequest();
    app_context_->launcher()->CreateApplication(std::move(launch_info), NULL);
    return services;
  }

  template <class Interface>
  fidl::InterfacePtr<Interface> ConnectToService(const std::string& url) {
    auto services = StartServiceProvider(url);
    return modular::ConnectToService<Interface>(services.get());
  }

  void StartAgent(const std::string& url) {
    auto agent_host =
        std::make_unique<maxwell::ApplicationEnvironmentHostImpl>();

    agent_host->AddService<maxwell::ContextPublisher>(
        [this, url](fidl::InterfaceRequest<maxwell::ContextPublisher> request) {
          context_engine_->RegisterContextAcquirer(url, std::move(request));
        });
    agent_host->AddService<maxwell::ContextPubSub>(
        [this, url](fidl::InterfaceRequest<maxwell::ContextPubSub> request) {
          context_engine_->RegisterContextAgent(url, std::move(request));
        });
    agent_host->AddService<maxwell::ContextSubscriber>([this, url](
        fidl::InterfaceRequest<maxwell::ContextSubscriber> request) {
      context_engine_->RegisterSuggestionAgent(url, std::move(request));
    });
    agent_host->AddService<maxwell::ProposalPublisher>([this, url](
        fidl::InterfaceRequest<maxwell::ProposalPublisher> request) {
      suggestion_engine_->RegisterSuggestionAgent(url, std::move(request));
    });

    agent_host->AddService<modular::FocusController>(
        [this](fidl::InterfaceRequest<modular::FocusController> request) {
          focus_controller_->Duplicate(std::move(request));
        });

    agent_launcher_.StartAgent(url, std::move(agent_host));
  }

  std::unique_ptr<modular::ApplicationContext> app_context_;

  fidl::BindingSet<maxwell::Launcher> launcher_bindings_;

  maxwell::ContextEnginePtr context_engine_;
  modular::ServiceProviderPtr suggestion_services_;
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
