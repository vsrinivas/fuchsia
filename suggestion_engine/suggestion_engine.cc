// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unordered_map>
#include <mojo/system/main.h>

#include "apps/maxwell/interfaces/proposal_manager.mojom.h"
#include "apps/maxwell/interfaces/suggestion_manager.mojom.h"

#include "apps/maxwell/bound_set.h"
#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/application/run_application.h"
#include "mojo/public/cpp/application/service_provider_impl.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/public/cpp/bindings/binding_set.h"
#include "mojo/public/cpp/bindings/strong_binding_set.h"

#include "apps/maxwell/debug.h"
#include "apps/maxwell/suggestion_engine/next_subscriber.h"

namespace {

using mojo::Binding;
using mojo::ConnectionContext;
using mojo::InterfaceHandle;
using mojo::InterfaceRequest;

using namespace maxwell::suggestion_engine;

class SuggestionEngine : public SuggestionManager {
 public:
  // SuggestionManager

  void SubscribeToInterruptions(
      InterfaceHandle<SuggestionListener> listener) override {
    // TODO(rosswang): no interruptions yet
  }

  void SubscribeToNext(InterfaceHandle<SuggestionListener> listener,
                       InterfaceRequest<NextController> controller) override {
    std::unique_ptr<NextSubscriber> sub(
        new NextSubscriber(&ranked_suggestions_, std::move(listener)));
    sub->Bind(std::move(controller));
    next_subscribers_.emplace(std::move(sub));
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

  // end SuggestionManager

  void GetProposalManager(const std::string& component,
                          InterfaceRequest<ProposalManager> request) {
    auto& source = sources_[component];
    if (!source)  // create if it didn't already exist
      source.reset(new SourceEntry(this, component));

    source->AddBinding(std::move(request));
  }

 private:
  // SourceEntry tracks proposals and their resulting suggestions from a single
  // suggestion agent. Source entries are created on demand and kept alive as
  // long as any proposals or publisher bindings exist.
  class SourceEntry : public ProposalManager {
   public:
    SourceEntry(SuggestionEngine* suggestinator,
                const std::string& component_url)
        : suggestinator_(suggestinator),
          component_url_(component_url),
          bindings_(this) {}

    void AddBinding(InterfaceRequest<ProposalManager> request) {
      bindings_.emplace(new Binding<ProposalManager>(this, std::move(request)));
    }

    void Propose(ProposalPtr proposal) override {
      const size_t old_size = suggestions_.size();
      Suggestion* suggestion = &suggestions_[proposal->id];

      if (suggestions_.size() > old_size)
        OnNewProposal(*proposal, suggestion);
      else
        OnChangeProposal(*proposal, suggestion);
    }

    void Remove(const mojo::String& proposal_id) override {
      const auto it = suggestions_.find(proposal_id);

      if (it != suggestions_.end()) {
        BroadcastRemoveSuggestion(it->second);
        suggestions_.erase(it);

        if (suggestions_.empty() && bindings_.empty())
          EraseSelf();
      }
    }

    void GetAll(const GetAllCallback& callback) override {
      // TODO
    }

   private:
    class BindingSet : public maxwell::BindingSet<ProposalManager> {
     public:
      BindingSet(SourceEntry* source_entry) : source_entry_(source_entry) {}

     protected:
      void OnConnectionError(Binding<ProposalManager>* binding) override {
        maxwell::BindingSet<ProposalManager>::OnConnectionError(binding);

        if (empty() && source_entry_->suggestions_.empty())
          source_entry_->EraseSelf();
      }

     private:
      SourceEntry* const source_entry_;
    };

    // TODO(rosswang): get rid of this
    static SuggestionDisplayPropertiesPtr ConvertDisplay(
        const ProposalDisplayProperties& pd) {
      SuggestionDisplayPropertiesPtr sd = SuggestionDisplayProperties::New();
      sd->icon = pd.icon;
      sd->headline = pd.headline;
      sd->subtext = pd.subtext;
      sd->details = pd.details;

      return sd;
    }

    void ProposalToSuggestion(const Proposal& proposal,
                              Suggestion* suggestion) {
      // TODO(rosswang): real UUIDs
      suggestion->uuid = std::to_string(reinterpret_cast<size_t>(this)) +
                         std::to_string(id_++);
      // TODO(rosswang): rank
      suggestion->rank = id_;  // shhh

      suggestion->display_properties = ConvertDisplay(*proposal.display);
    }

    void BroadcastNewSuggestion(const Suggestion& suggestion) {
      for (const auto& subscriber : suggestinator_->next_subscribers_)
        subscriber->OnNewSuggestion(suggestion);
    }

    void BroadcastRemoveSuggestion(const Suggestion& suggestion) {
      for (const auto& subscriber : suggestinator_->next_subscribers_)
        subscriber->BeforeRemoveSuggestion(suggestion);
    }

    void OnNewProposal(const Proposal& proposal, Suggestion* suggestion) {
      ProposalToSuggestion(proposal, suggestion);

      // TODO(rosswang): sort
      suggestinator_->ranked_suggestions_.emplace_back(suggestion);

      BroadcastNewSuggestion(*suggestion);
    }

    void OnChangeProposal(const Proposal& proposal, Suggestion* suggestion) {
      BroadcastRemoveSuggestion(*suggestion);

      // TODO(rosswang): re-rank if necessary
      suggestion->display_properties = ConvertDisplay(*proposal.display);

      BroadcastNewSuggestion(*suggestion);
    }

    void EraseSelf() { suggestinator_->sources_.erase(component_url_); }

    SuggestionEngine* const suggestinator_;
    const std::string component_url_;
    std::unordered_map<std::string, Suggestion> suggestions_;
    BindingSet bindings_;

    uint64_t id_;
  };

  std::unordered_map<std::string, std::unique_ptr<SourceEntry>> sources_;
  std::vector<Suggestion*> ranked_suggestions_;
  maxwell::BindingSet<NextController,
                      std::unique_ptr<NextSubscriber>,
                      NextSubscriber::GetBinding>
      next_subscribers_;
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
          suggestinator_.GetProposalManager(connection_context.remote_url,
                                            std::move(request));
        });
    service_provider_impl->AddService<SuggestionManager>([this](
        const ConnectionContext& connection_context,
        InterfaceRequest<SuggestionManager> request) {
      if (connection_context.remote_url == kSysUiUrl) {
        suggestion_bindings_.AddBinding(&suggestinator_, std::move(request));
      }
    });
    debug_.AddService(shell(), service_provider_impl);
    return true;
  }

 private:
  maxwell::DebugSupport debug_;
  SuggestionEngine suggestinator_;
  mojo::BindingSet<SuggestionManager> suggestion_bindings_;

  MOJO_DISALLOW_COPY_AND_ASSIGN(SuggestionEngineApp);
};

}  // namespace

MojoResult MojoMain(MojoHandle request) {
  SuggestionEngineApp app;
  return mojo::RunApplication(request, &app);
}
