// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unordered_map>

#include "apps/maxwell/services/suggestion_engine.fidl.h"

#include "apps/maxwell/bound_set.h"
#include "apps/modular/lib/app/application_context.h"
#include "apps/modular/lib/app/connect.h"
#include "lib/mtl/tasks/message_loop.h"

#include "apps/maxwell/suggestion_engine/next_subscriber.h"

namespace {

using fidl::Binding;
using fidl::InterfaceHandle;
using fidl::InterfaceRequest;

using namespace maxwell::suggestion_engine;

class SuggestionEngineImpl : public SuggestionEngine, public SuggestionManager {
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

  void NotifyInteraction(const fidl::String& suggestion_uuid,
                         SuggestionInteractionPtr interaction) override {
    FTL_LOG(INFO) << (interaction->type == SuggestionInteractionType::SELECTED
                          ? "Accepted"
                          : "Dismissed")
                  << " suggestion " << suggestion_uuid << ")";
  }

  // end SuggestionManager

  // SuggestionEngine

  void RegisterSuggestionAgent(
      const fidl::String& url,
      InterfaceRequest<ProposalManager> proposal_manager) override {
    auto& source = sources_[url];
    if (!source)  // create if it didn't already exist
      source.reset(new SourceEntry(this, url));

    source->AddBinding(std::move(proposal_manager));
  }

  // end SuggestionEngine

 private:
  // SourceEntry tracks proposals and their resulting suggestions from a single
  // suggestion agent. Source entries are created on demand and kept alive as
  // long as any proposals or publisher bindings exist.
  class SourceEntry : public ProposalManager {
   public:
    SourceEntry(SuggestionEngineImpl* suggestinator,
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

    void Remove(const fidl::String& proposal_id) override {
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

    void ProposalToSuggestion(const Proposal& proposal,
                              Suggestion* suggestion) {
      // TODO(rosswang): real UUIDs
      suggestion->uuid = std::to_string(reinterpret_cast<size_t>(this)) +
                         std::to_string(id_++);
      // TODO(rosswang): rank
      suggestion->rank = id_;  // shhh

      suggestion->display_properties = proposal.display->Clone();
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
      suggestion->display_properties = proposal.display->Clone();

      BroadcastNewSuggestion(*suggestion);
    }

    void EraseSelf() { suggestinator_->sources_.erase(component_url_); }

    SuggestionEngineImpl* const suggestinator_;
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

class SuggestionEngineApp {
 public:
  SuggestionEngineApp()
      : app_ctx_(modular::ApplicationContext::CreateFromStartupInfo()) {
    app_ctx_->outgoing_services()->AddService<SuggestionEngine>(
        [this](InterfaceRequest<SuggestionEngine> request) {
          admin_bindings_.AddBinding(&suggestinator_, std::move(request));
        });
    app_ctx_->outgoing_services()->AddService<SuggestionManager>(
        [this](InterfaceRequest<SuggestionManager> request) {
          suggestion_bindings_.AddBinding(&suggestinator_, std::move(request));
        });
  }

 private:
  std::unique_ptr<modular::ApplicationContext> app_ctx_;

  SuggestionEngineImpl suggestinator_;
  fidl::BindingSet<SuggestionEngine> admin_bindings_;
  fidl::BindingSet<SuggestionManager> suggestion_bindings_;
};

}  // namespace

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  SuggestionEngineApp app;
  loop.Run();
  return 0;
}
