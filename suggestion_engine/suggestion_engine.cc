// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mojo/system/main.h>

#include "apps/maxwell/interfaces/proposal_manager.mojom.h"
#include "apps/maxwell/interfaces/suggestion_manager.mojom.h"
#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/application/run_application.h"
#include "mojo/public/cpp/application/service_provider_impl.h"
#include "mojo/public/cpp/bindings/binding_set.h"
#include "mojo/public/cpp/bindings/strong_binding_set.h"

namespace {

using mojo::ConnectionContext;
using mojo::InterfaceHandle;
using mojo::InterfaceRequest;

using namespace maxwell::suggestion_engine;

class SuggestionManagerImpl : public SuggestionManager {
 public:
  void SubscribeToInterruptions(
      InterfaceHandle<SuggestionListener> listener) override {
    // TODO(rosswang): no interruptions yet
  }

  void SubscribeToNext(InterfaceHandle<SuggestionListener> listener,
                       InterfaceRequest<NextController> controller) override {
    // TODO(rosswang)
  }

  void InitiateAsk(InterfaceHandle<SuggestionListener> listener,
                   InterfaceRequest<AskController> controller) override {
    // TODO(rosswang): no ask handlers yet
  }

  void NotifyInteraction(const mojo::String& suggestion_uuid,
                         SuggestionInteractionPtr interaction) override {
    MOJO_LOG(INFO) << (interaction->type == SuggestionInteractionType::SELECTED
                           ? "Accepted"
                           : "Dismissed")
                   << " suggestion " << suggestion_uuid << ")";
  }

  void Propose(const ProposalPtr proposal) {
    SuggestionPtr s = Suggestion::New();
    // TODO(rosswang): real UUIDs
    s->uuid = std::to_string(id_++);

    // TODO(rosswang): rank

    // TODO(rosswang): What was the rationale for diverging these?
    ProposalDisplayPropertiesPtr& pd = proposal->display;
    SuggestionDisplayPropertiesPtr sd = SuggestionDisplayProperties::New();
    sd->icon = pd->icon;
    sd->headline = pd->headline;
    sd->subtext = pd->subtext;
    sd->details = pd->details;

    s->display_properties = std::move(sd);

    MOJO_LOG(INFO) << "Adding suggestion " << s->uuid << ": "
                   << s->display_properties->headline;
    suggestions_.emplace_back(std::move(s));
  }

 private:
  std::vector<SuggestionPtr> suggestions_;
  uint64_t id_ = 0;
};

class ProposalManagerImpl : public ProposalManager {
 public:
  ProposalManagerImpl(const std::string& component,
                      SuggestionManagerImpl* suggestinator)
      : suggestinator_(suggestinator) {
    // TODO(rosswang): suggestion attribution
  }

  void Propose(ProposalPtr proposal) override {
    suggestinator_->Propose(std::move(proposal));
  }

  void Remove(const mojo::String& proposal_id) override {
    // TODO
    MOJO_LOG(INFO) << "Remove proposal " << proposal_id;
  }

  void GetAll(const GetAllCallback& callback) override {
    // TODO
  }

 private:
  SuggestionManagerImpl* suggestinator_;
};

constexpr char kSysUiUrl[] = "mojo:maxwell_test";

class SuggestionEngineApp : public mojo::ApplicationImplBase {
 public:
  SuggestionEngineApp() {}

  bool OnAcceptConnection(
      mojo::ServiceProviderImpl* service_provider_impl) override {
    service_provider_impl->AddService<ProposalManager>(
        [this](const ConnectionContext& connection_context,
               InterfaceRequest<ProposalManager> request) {
          proposal_bindings_.AddBinding(
              new ProposalManagerImpl(connection_context.remote_url,
                                      &suggestinator_),
              std::move(request));
        });
    service_provider_impl->AddService<SuggestionManager>([this](
        const ConnectionContext& connection_context,
        InterfaceRequest<SuggestionManager> request) {
      if (connection_context.remote_url == kSysUiUrl) {
        suggestion_bindings_.AddBinding(&suggestinator_, std::move(request));
      }
    });
    return true;
  }

 private:
  SuggestionManagerImpl suggestinator_;
  mojo::StrongBindingSet<ProposalManager> proposal_bindings_;
  mojo::BindingSet<SuggestionManager> suggestion_bindings_;

  MOJO_DISALLOW_COPY_AND_ASSIGN(SuggestionEngineApp);
};

}  // namespace

MojoResult MojoMain(MojoHandle request) {
  SuggestionEngineApp app;
  return mojo::RunApplication(request, &app);
}
