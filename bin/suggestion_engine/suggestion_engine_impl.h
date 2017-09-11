// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>
#include <string>
#include <unordered_map>

#include "lib/app/cpp/application_context.h"

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

#include "apps/maxwell/services/context/context_writer.fidl.h"
#include "apps/maxwell/services/suggestion/suggestion_engine.fidl.h"
#include "apps/modular/services/story/story_provider.fidl.h"
#include "apps/modular/services/user/focus.fidl.h"

#include "lib/fidl/cpp/bindings/interface_ptr_set.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace maxwell {

const std::string kQueryContextKey = "/suggestion_engine/current_query";

// This class is currently responsible for 3 things:
//
// 1) Maintaining repositories of ranked Suggestions (stored inside
//    the RankedSuggestions class) for both Ask and Next proposals.
//  a) Ask suggestions are issued by AskHandlers, in a pull-based model
//     in response to Ask queries. Ask queries are issued via the
//     DispatchAsk method, and Suggestions are asynchronously returned
//     through DispatchAsk's callback.
//
//     The set of Ask proposals for the latest query are currently
//     buffered in the ask_suggestions_ member, though this process can
//     be made entirely stateless.
//
//  b) Next suggestions are issued by ProposalPublishers through the
//     Propose method, and can be issued at any time. These proposals
//     are stored in the next_suggestions_ member.
//
//   Whenever a RankedSuggestions object is updated, that update is pushed
//   to its registered subscribers (SuggestionSubscribers). These subscribers
//   are registered on a SuggestionChannel object -- each RankedSuggestions
//   object has a single SuggestionChannel.
//
// 2) Storing FIDL bindings for AskHandlers and ProposalPublishers.
//
//  a) ProposalPublishers (for Next Suggestions) can be registered via the
//     RegisterPublisher method.
//
//  b) AskHandlers are currently registered through
//     ProposalPublisher.RegisterAskHandler, but this is unnecessary coupling
//     between the ProposalPublisher (a Next interface) and AskHandler (an Ask
//     interface), so this should eventually be changed with the addition of
//     SuggestionEngine.RegisterAskHandler
//
// 3) Acts as a SuggestionProvider for those wishing to subscribe to
//    Suggestions.
class SuggestionEngineImpl : public SuggestionEngine,
                             public SuggestionProvider,
                             public AskDispatcher {
 public:
  SuggestionEngineImpl();

  // TODO(andrewosh): The following two methods should be removed. New
  // ProposalPublishers should be created whenever they're requested, and they
  // should be erased automatically when the client disconnects (they should be
  // stored in a BindingSet with an error handler that performs removal).
  ProposalPublisherImpl* GetOrCreateSourceClient(
      const std::string& component_url);

  void RemoveSourceClient(const std::string& component_url) {
    proposal_publishers_.erase(component_url);
  };

  // Should only be called from ProposalPublisherImpl.
  void AddNextProposal(ProposalPublisherImpl* source, ProposalPtr proposal);

  // Should only be called from ProposalPublisherImpl.
  void AddAskProposal(ProposalPublisherImpl* source, ProposalPtr proposal);

  // Should only be called from ProposalPublisherImpl.
  void RemoveProposal(const std::string& component_url,
                      const std::string& proposal_id);

  // |SuggestionProvider|
  void SubscribeToInterruptions(
      fidl::InterfaceHandle<SuggestionListener> listener) override;

  // |SuggestionProvider|
  void SubscribeToNext(
      fidl::InterfaceHandle<SuggestionListener> listener,
      fidl::InterfaceRequest<NextController> controller) override;

  // The way Asks are currently handled is confusing, but can be understood as
  // follows:
  //
  // Asks are currently logically pull-based, but implemented on top of a
  // push-based design. This should be changed. Within this system, the
  // AskSubscriber has two responsibilities:
  //  1) Take the queries passed to the `controller` (via SetUserQuery) and hand
  //     those off to the SuggestionEngineImpl's (or whichever class implements
  //     AskDispatcher) DispatchAsk method. DispatchAsk will fan the query out
  //     to all registered AskHandlers and the results are pushed into
  //     ask_suggestions_.
  //
  //  2) Since ask_suggestions_ is a RankedSuggestions, it has a
  //     SuggestionChannel with registered SuggestionSubscribers. The
  //     AskSubscriber is a SuggestionSubscriber, and so also functions as a
  //     proxy to pass the latest query results back to `listener`.
  //
  // TODO: This process can be refactored to eliminate the need for
  // ask_suggestions_
  //
  // |SuggestionProvider|
  //
  void InitiateAsk(fidl::InterfaceHandle<SuggestionListener> listener,
                   fidl::InterfaceRequest<AskController> controller) override;

  // When a user interacts with a Suggestion, the suggestion engine will be
  // notified of consumed suggestion's ID. With this, we will do two things:
  //
  // 1) Perform the Action contained in the Suggestion
  //    (suggestion->proposal->on_selected)
  //
  //    Action handling should be extracted into separate classes to simplify
  //    SuggestionEngineImpl (i.e. an ActionManager which delegates action
  //    execution to ActionHandlers based on the Action's tag).
  //
  // 2) Remove consumed Suggestion from our suggestion repositories
  //    (ask_suggestions_ and next_suggestions_).
  //
  //    As described in the top-level comment, once the Ask pathway is made
  //    entirely stateless, this will only need to remove the corresponding
  //    suggestion from next_suggestions_.
  //
  // |SuggestionProvider|
  //
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
      fidl::InterfaceHandle<ContextWriter> context_writer) override;

  // |AskDispatcher|
  void DispatchAsk(UserInputPtr input) override;

  void AddAskPublisher(std::unique_ptr<AskPublisher> publisher);

 private:
  // Searches for a SuggestionPrototype in the Next and Ask lists.
  SuggestionPrototype* FindSuggestion(std::string suggestion_id);

  // This method is only required because the Ask pathway is not entirely
  // stateless. Whenever a new Ask query is issued, ask_suggestions_ is emptied,
  // all AskSubscribers are invalidated (they're notified that they should
  // re-fetch all Ask Suggestions).
  //
  // After the query is completed, the now-empty ask_suggestions_ is
  // repopulated with the latest results.
  void RemoveAllAskSuggestions();

  SuggestionPrototype* CreateSuggestionPrototype(ProposalPublisherImpl* source,
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

  // Both story_provider_ and focus_provider_ptr are used exclusively during
  // Action execution (in the PerformActions call inside NotifyInteraction).
  //
  // These are required to create new Stories and interact with the current
  // Story.
  modular::StoryProviderPtr story_provider_;
  fidl::InterfacePtr<modular::FocusProvider> focus_provider_ptr_;

  // Watches for changes in StoryInfo from the StoryProvider, acts as a filter
  // for Proposals on all channels, and notifies when there are changes so that
  // we can re-filter Proposals.
  //
  // Initialized late in Initialize().
  std::unique_ptr<TimelineStoriesWatcher> timeline_stories_watcher_;

  // TODO(thatguy): All Channels also get a ReevaluateFilters method, which
  // would remove Suggestions that are now filtered or add
  // new ones that are no longer filtered.

  // The repository of raw suggestion prototypes.
  std::map<std::pair<std::string, std::string>,
           std::unique_ptr<SuggestionPrototype>>
      suggestion_prototypes_;

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

  // The ContextWriter that publishes the current user query to the
  // ContextEngine.
  ContextWriterPtr context_writer_;

  // The debugging interface for all Suggestions.
  SuggestionDebugImpl debug_;
};

}  // namespace maxwell
