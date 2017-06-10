// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unordered_map>

#include "apps/maxwell/src/suggestion_engine/ask_channel.h"
#include "apps/maxwell/src/suggestion_engine/filter.h"
#include "apps/maxwell/src/suggestion_engine/next_channel.h"
#include "apps/maxwell/src/suggestion_engine/proposal_publisher_impl.h"
#include "apps/maxwell/src/suggestion_engine/suggestion_prototype.h"
#include "apps/maxwell/services/context/context_publisher.fidl.h"
#include "lib/fidl/cpp/bindings/interface_ptr_set.h"
#include "lib/ftl/memory/weak_ptr.h"

namespace maxwell {

const std::string kQueryContextKey = "/suggestion_engine/current_query";

class Repo {
 public:
  Repo(ProposalFilter filter, ContextPublisherPtr publisher)
      : next_channel_(filter),
        filter_(filter),
        publisher_(std::move(publisher)) {}

  ProposalPublisherImpl* GetOrCreateSourceClient(
      const std::string& component_url);

  void RemoveSourceClient(const std::string& component_url) {
    sources_.erase(component_url);
  }

  // Should only be called from ProposalPublisherImpl
  // If |channel| is null, the suggestion is added to all channels.
  // TODO(rosswang): Implement derived channels instead if such behavior would
  // still be reasonable after the upcoming redesign.
  void AddSuggestion(SuggestionPrototype* prototype,
                     SuggestionChannel* channel);
  // Should only be called from ProposalPublisherImpl
  void RemoveSuggestion(const std::string& id) { suggestions_.erase(id); }

  void SubscribeToNext(fidl::InterfaceHandle<SuggestionListener> listener,
                       fidl::InterfaceRequest<NextController> controller) {
    next_channel_.AddSubscriber(std::make_unique<NextSubscriber>(
        next_channel_.ranked_suggestions(), std::move(listener),
        std::move(controller)));
  }

  void SubscribeToInterruptions(
      fidl::InterfaceHandle<SuggestionListener> listener) {
    next_channel_.AddInterruptionsSubscriber(
        std::make_unique<InterruptionsSubscriber>(std::move(listener)));
  }

  void InitiateAsk(fidl::InterfaceHandle<SuggestionListener> listener,
                   fidl::InterfaceRequest<AskController> controller);

  void AddAskHandler(fidl::InterfaceHandle<AskHandler> ask_handler,
                     ftl::WeakPtr<ProposalPublisherImpl> publisher) {
    ask_handlers_.emplace(std::make_unique<AskPublisher>(
        AskHandlerPtr::Create(std::move(ask_handler)), publisher));
  }

  void DispatchAsk(UserInputPtr query, AskChannel* channel) {
    publisher_->Publish(kQueryContextKey, query->get_text());
    for (const std::unique_ptr<AskPublisher>& ask : ask_handlers_) {
      ask->handler->Ask(
          query.Clone(), [&ask, channel](fidl::Array<ProposalPtr> proposals) {
            channel->DirectProposal(ask->publisher.get(), std::move(proposals));
          });
    }
  }

  std::unique_ptr<SuggestionPrototype> Extract(const std::string& id);

  // Non-mutating indexer; returns NULL if no such suggestion exists.
  const SuggestionPrototype* operator[](
      const std::string& suggestion_id) const {
    auto it = suggestions_.find(suggestion_id);
    return it == suggestions_.end() ? NULL : it->second;
  }

  ProposalFilter filter() { return filter_; }

  NextChannel* next_channel() { return &next_channel_; }

 private:
  // This struct allows proper ownership and lifecycle management of proposals
  // produced during Ask so that they are namespaced by publisher like Next
  // proposals.
  struct AskPublisher {
    AskHandlerPtr handler;
    ftl::WeakPtr<ProposalPublisherImpl> const publisher;

    AskPublisher(AskHandlerPtr handler,
                 ftl::WeakPtr<ProposalPublisherImpl> publisher)
        : handler(std::move(handler)), publisher(publisher) {}

    static AskHandlerPtr* GetHandler(std::unique_ptr<AskPublisher>* ask) {
      return &(*ask)->handler;
    }
  };

  std::string RandomUuid() {
    static uint64_t id = 0;
    // TODO(rosswang): real UUIDs
    return std::to_string(id++);
  }

  std::unordered_map<std::string, std::unique_ptr<ProposalPublisherImpl>>
      sources_;
  // indexed by suggestion ID
  std::unordered_map<std::string, SuggestionPrototype*> suggestions_;
  NextChannel next_channel_;
  maxwell::BoundNonMovableSet<AskChannel> ask_channels_;
  maxwell::BoundPtrSet<AskHandler,
                       std::unique_ptr<AskPublisher>,
                       AskPublisher::GetHandler>
      ask_handlers_;

  ProposalFilter filter_;

  ContextPublisherPtr publisher_;
};

}  // namespace maxwell
