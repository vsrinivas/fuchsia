// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unordered_map>

#include "apps/maxwell/services/suggestion/suggestion_engine.fidl.h"

#include "apps/maxwell/src/bound_set.h"
#include "apps/modular/lib/app/application_context.h"

#include "apps/maxwell/src/suggestion_engine/next_subscriber.h"
#include "apps/maxwell/src/suggestion_engine/suggestion_agent_client_impl.h"

namespace maxwell {
namespace suggestion {

class SuggestionAgentClientImpl;

class SuggestionEngineApp : public SuggestionEngine, public ShellClient {
 public:
  SuggestionEngineApp()
      : app_context_(modular::ApplicationContext::CreateFromStartupInfo()) {
    app_context_->outgoing_services()->AddService<SuggestionEngine>(
        [this](fidl::InterfaceRequest<SuggestionEngine> request) {
          bindings_.AddBinding(this, std::move(request));
        });
  }

  // ShellClient

  void SubscribeToInterruptions(
      fidl::InterfaceHandle<Listener> listener) override {
    // TODO(rosswang): no interruptions yet
  }

  void SubscribeToNext(
      fidl::InterfaceHandle<Listener> listener,
      fidl::InterfaceRequest<NextController> controller) override {
    std::unique_ptr<NextSubscriber> sub(
        new NextSubscriber(&ranked_suggestions_, std::move(listener)));
    sub->Bind(std::move(controller));
    next_subscribers_.emplace(std::move(sub));
  }

  void InitiateAsk(fidl::InterfaceHandle<Listener> listener,
                   fidl::InterfaceRequest<AskController> controller) override {
    // TODO(rosswang): no ask handlers yet
  }

  void NotifyInteraction(const fidl::String& suggestion_uuid,
                         InteractionPtr interaction) override {
    FTL_LOG(INFO) << (interaction->type == InteractionType::SELECTED
                          ? "Accepted"
                          : "Dismissed")
                  << " suggestion " << suggestion_uuid << ")";
  }

  // end ShellClient

  // SuggestionEngine

  void RegisterSuggestionAgent(
      const fidl::String& url,
      fidl::InterfaceRequest<SuggestionAgentClient> client) override;

  void GetShellClient(fidl::InterfaceRequest<ShellClient> client) override {
    shell_client_bindings_.AddBinding(this, std::move(client));
  }

  // end SuggestionEngine

 private:
  friend class SuggestionAgentClientImpl;

  std::unique_ptr<modular::ApplicationContext> app_context_;

  std::unordered_map<std::string, std::unique_ptr<SuggestionAgentClientImpl>>
      sources_;
  std::vector<Suggestion*> ranked_suggestions_;

  fidl::BindingSet<SuggestionEngine> bindings_;
  maxwell::BindingSet<NextController,
                      std::unique_ptr<NextSubscriber>,
                      NextSubscriber::GetBinding>
      next_subscribers_;
  fidl::BindingSet<ShellClient> shell_client_bindings_;
};

}  // namespace suggestion
}  // namespace maxwell
