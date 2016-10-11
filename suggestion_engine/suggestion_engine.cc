// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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

namespace {

using mojo::Binding;
using mojo::ConnectionContext;
using mojo::InterfaceHandle;
using mojo::InterfaceRequest;

using namespace maxwell::suggestion_engine;

// TODO(rosswang): Ask is probably the more general case, but we probably want
// a direct propagation channel for agents to be sensitive to Asks (as well as
// an indirect context channel to catch agents that weren't engineered for Ask).
class NextSubscriber : public NextController {
 public:
  static Binding<NextController>* GetBinding(
      std::unique_ptr<NextSubscriber>* next_subscriber) {
    return &(*next_subscriber)->binding_;
  }

  NextSubscriber(std::vector<SuggestionPtr>* ranked_suggestions,
                 InterfaceHandle<SuggestionListener> listener)
      : binding_(this),
        ranked_suggestions_(ranked_suggestions),
        listener_(SuggestionListenerPtr::Create(std::move(listener))) {}

  void Bind(InterfaceRequest<NextController> request) {
    binding_.Bind(std::move(request));
  }

  void SetResultCount(int32_t count) override {
    size_t effective_count =
        std::min((size_t)count, ranked_suggestions_->size());

    if (effective_count != (size_t)max_results_) {
      if (effective_count > (size_t)max_results_) {
        mojo::Array<SuggestionPtr> delta;
        for (size_t i = max_results_; i < effective_count; i++) {
          delta.push_back((*ranked_suggestions_)[i].Clone());
        }
        listener_->OnAdd(std::move(delta));
      } else if (effective_count == 0) {
        listener_->OnRemoveAll();
      } else if (effective_count < (size_t)max_results_) {
        for (size_t i = max_results_ - 1; i >= effective_count; i--) {
          listener_->OnRemove((*ranked_suggestions_)[i]->uuid);
        }
      }
    }

    max_results_ = count;
  }

  void OnNewSuggestion(const SuggestionPtr& suggestion) {
    if (IncludeSuggestion(suggestion)) {
      mojo::Array<SuggestionPtr> batch;
      batch.push_back(suggestion.Clone());
      listener_->OnAdd(std::move(batch));
    }
  }

  void BeforeRemovedSuggestion(const SuggestionPtr& suggestion) {
    if (IncludeSuggestion(suggestion)) {
      listener_->OnRemove(suggestion->uuid);
    }
  }

 private:
  // A suggestion should be included if its sorted index (by rank) is less than
  // max_results_. We don't have to do a full iteration here since we can just
  // compare the rank with the tail for all but the edge case where ranks are
  // identical.
  bool IncludeSuggestion(const SuggestionPtr& suggestion) const {
    if (max_results_ == 0)
      return false;
    if (ranked_suggestions_->size() <= (size_t)max_results_)
      return true;

    float newRank = suggestion->rank;

    int32_t i = max_results_ - 1;
    auto it = ranked_suggestions_->begin() + i;

    if (newRank > (*it)->rank)
      return false;

    if (newRank < (*it)->rank)
      return true;

    // else we actually have to iterate. Iterate until the rank is less than
    // the new suggestion, at which point we can conclude that the new
    // suggestion has not made it into the window.
    do {
      // Could also compare UUIDs
      if (it->get() == suggestion.get()) {
        return true;
      }

      // backwards iteration is inelegant.
      if (it == ranked_suggestions_->begin())
        return false;

      --it;
    } while (newRank == (*it)->rank);

    return false;
  }

  Binding<NextController> binding_;
  // An upper bound on the number of suggestions to offer this subscriber, as
  // given by SetResultCount.
  int32_t max_results_ = 0;
  std::vector<SuggestionPtr>* ranked_suggestions_;
  SuggestionListenerPtr listener_;
};

class SuggestionManagerImpl : public SuggestionManager {
 public:
  void SubscribeToInterruptions(
      InterfaceHandle<SuggestionListener> listener) override {
    // TODO(rosswang): no interruptions yet
  }

  void SubscribeToNext(InterfaceHandle<SuggestionListener> listener,
                       InterfaceRequest<NextController> controller) override {
    std::unique_ptr<NextSubscriber> sub(
        new NextSubscriber(&suggestions_, std::move(listener)));
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

  void Propose(const ProposalPtr proposal) {
    SuggestionPtr s = Suggestion::New();
    // TODO(rosswang): real UUIDs
    s->uuid = std::to_string(id_++);

    // TODO(rosswang): rank
    s->rank = id_;  // shhh

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

    // Broadcast to subscribers
    const SuggestionPtr& s_ref = suggestions_.back();
    for (const auto& subscriber : next_subscribers_) {
      subscriber->OnNewSuggestion(s_ref);
    }
  }

 private:
  std::vector<SuggestionPtr> suggestions_;
  uint64_t id_ = 0;
  maxwell::BoundSet<NextController,
                    mojo::Binding<NextController>,
                    std::unique_ptr<NextSubscriber>,
                    NextSubscriber::GetBinding>
      next_subscribers_;
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
