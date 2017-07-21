// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>
#include <unordered_map>

#include "application/lib/app/application_context.h"

#include "apps/maxwell/src/bound_set.h"
#include "apps/maxwell/src/suggestion_engine/ask_dispatcher.h"
#include "apps/maxwell/src/suggestion_engine/ask_publisher.h"
#include "apps/maxwell/src/suggestion_engine/debug.h"
#include "apps/maxwell/src/suggestion_engine/filter.h"
#include "apps/maxwell/src/suggestion_engine/proposal_publisher_impl.h"
#include "apps/maxwell/src/suggestion_engine/ranked_suggestions.h"
#include "apps/maxwell/src/suggestion_engine/ranking.h"
#include "apps/maxwell/src/suggestion_engine/suggestion_prototype.h"
#include "apps/maxwell/src/suggestion_engine/timeline_stories_filter.h"
#include "apps/maxwell/src/suggestion_engine/timeline_stories_watcher.h"

#include "apps/maxwell/services/context/context_publisher.fidl.h"
#include "apps/maxwell/services/suggestion/suggestion_engine.fidl.h"
#include "apps/modular/services/story/story_provider.fidl.h"
#include "apps/modular/services/user/focus.fidl.h"

#include "lib/fidl/cpp/bindings/interface_ptr_set.h"
#include "lib/ftl/memory/weak_ptr.h"

namespace maxwell {

const std::string kQueryContextKey = "/suggestion_engine/current_query";

class SuggestionEngineImpl : public SuggestionEngine,
                             public SuggestionProvider,
                             public AskDispatcher {
 public:
  SuggestionEngineImpl()
      : app_context_(app::ApplicationContext::CreateFromStartupInfo()),
        ask_suggestions_(new RankedSuggestions(&ask_channel_)),
        next_suggestions_(new RankedSuggestions(&next_channel_)) {
    app_context_->outgoing_services()->AddService<SuggestionEngine>(
        [this](fidl::InterfaceRequest<SuggestionEngine> request) {
          bindings_.AddBinding(this, std::move(request));
        });
    app_context_->outgoing_services()->AddService<SuggestionProvider>(
        [this](fidl::InterfaceRequest<SuggestionProvider> request) {
          suggestion_provider_bindings_.AddBinding(this, std::move(request));
        });
    app_context_->outgoing_services()->AddService<SuggestionDebug>(
        [this](fidl::InterfaceRequest<SuggestionDebug> request) {
          debug_bindings_.AddBinding(&debug_, std::move(request));
        });

    // The Next suggestions are always ranked with a static ranking function.
    next_suggestions_->UpdateRankingFunction(
        maxwell::ranking::GetNextRankingFunction());
  }

  ProposalPublisherImpl* GetOrCreateSourceClient(
      const std::string& component_url);

  void RemoveSourceClient(const std::string& component_url) {
    proposal_publishers_.erase(component_url);
  };

  // Should only be called from ProposalPublisherImpl.
  void AddNextProposal(ProposalPublisherImpl* source, ProposalPtr prototype);

  // Should only be called from ProposalPublisherImpl.
  void AddAskProposal(ProposalPublisherImpl* source, ProposalPtr prototype);

  // Should only be called from ProposalPublisherImpl.
  void RemoveProposal(const std::string& component_url,
                      const std::string& proposal_id);

  // Searches for a SuggestionPrototype in the Next and Ask lists.
  const SuggestionPrototype* FindSuggestion(std::string suggestion_id);

  // |SuggestionProvider|
  void SubscribeToInterruptions(
      fidl::InterfaceHandle<SuggestionListener> listener) override;

  // |SuggestionProvider|
  void SubscribeToNext(
      fidl::InterfaceHandle<SuggestionListener> listener,
      fidl::InterfaceRequest<NextController> controller) override;

  // |SuggestionProvider|
  void InitiateAsk(fidl::InterfaceHandle<SuggestionListener> listener,
                   fidl::InterfaceRequest<AskController> controller) override;

  // |SuggestionProvider|
  void NotifyInteraction(const fidl::String& suggestion_uuid,
                         InteractionPtr interaction) override;

  // |SuggestionEngine|
  void RegisterPublisher(
      const fidl::String& url,
      fidl::InterfaceRequest<ProposalPublisher> client) override;

  // |SuggestionEngine|
  void Initialize(
      fidl::InterfaceHandle<modular::StoryProvider> story_provider,
      fidl::InterfaceHandle<modular::FocusProvider> focus_provider,
      fidl::InterfaceHandle<ContextPublisher> context_publisher) override;

  // |AskDispatcher|
  void DispatchAsk(UserInputPtr input) override;

  void AddAskPublisher(std::unique_ptr<AskPublisher> publisher);

 private:
  SuggestionPrototype* CreateSuggestion(ProposalPublisherImpl* source,
                                        ProposalPtr proposal);
  std::string RandomUuid() {
    static uint64_t id = 0;
    // TODO(rosswang): real UUIDs
    return std::to_string(id++);
  }

  // TODO(andrewosh): Performing actions should be handled by a separate
  // interface that's passed to the SuggestionEngineImpl.
  void PerformActions(const fidl::Array<maxwell::ActionPtr>& actions,
                      uint32_t story_color);

  std::unique_ptr<app::ApplicationContext> app_context_;

  fidl::BindingSet<SuggestionEngine> bindings_;
  fidl::BindingSet<SuggestionProvider> suggestion_provider_bindings_;
  fidl::BindingSet<SuggestionDebug> debug_bindings_;

  modular::StoryProviderPtr story_provider_;
  fidl::InterfacePtr<modular::FocusProvider> focus_provider_ptr_;

  ContextPublisherPtr context_publisher_;

  // Watches for changes in StoryInfo from the StoryProvider, acts as a filter
  // for Proposals on all channels, and notifies when there are changes so that
  // we can re-filter Proposals.
  //
  // Initialized late in Initialize().
  std::unique_ptr<TimelineStoriesWatcher> timeline_stories_watcher_;

  // TODO(thatguy): All Channels also get a ReevaluateFilters method, which
  // would remove Suggestions that are now filtered or add
  // new ones that are no longer filtered.

  // Channels that dispatch outbound suggestions to SuggestionListeners.
  SuggestionChannel ask_channel_;
  RankedSuggestions* ask_suggestions_;

  SuggestionChannel next_channel_;
  RankedSuggestions* next_suggestions_;

  SuggestionChannel interruption_channel_;

  // The set of all ProposalPublishers that have registered to receive Asks.
  maxwell::BoundPtrSet<AskHandler,
                       std::unique_ptr<AskPublisher>,
                       AskPublisher::GetHandler>
      ask_handlers_;

  // The ProposalPublishers that have registered with the SuggestionEngine.
  std::unordered_map<std::string, std::unique_ptr<ProposalPublisherImpl>>
      proposal_publishers_;

  // TODO(andrewosh): Why is this necessary at this level?
  ProposalFilter filter_;

  // The ContextPublisher that publishes the current user query to the
  // ContextEngine.
  ContextPublisherPtr publisher_;

  // The debugging interface for all Suggestions.
  SuggestionDebugImpl debug_;
};

}  // namespace maxwell
