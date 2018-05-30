// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SUGGESTION_ENGINE_SUGGESTION_ENGINE_IMPL_H_
#define PERIDOT_BIN_SUGGESTION_ENGINE_SUGGESTION_ENGINE_IMPL_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <map>
#include <string>
#include <vector>

#include "lib/fidl/cpp/interface_ptr_set.h"
#include "lib/fxl/memory/weak_ptr.h"

#include "peridot/bin/suggestion_engine/auto_select_first_query_listener.h"
#include "peridot/bin/suggestion_engine/debug.h"
#include "peridot/bin/suggestion_engine/interruptions_processor.h"
#include "peridot/bin/suggestion_engine/next_processor.h"
#include "peridot/bin/suggestion_engine/proposal_publisher_impl.h"
#include "peridot/bin/suggestion_engine/query_processor.h"
#include "peridot/bin/suggestion_engine/ranked_suggestions_list.h"
#include "peridot/bin/suggestion_engine/timeline_stories_filter.h"
#include "peridot/bin/suggestion_engine/timeline_stories_watcher.h"
#include "peridot/lib/bound_set/bound_set.h"

namespace fuchsia {
namespace modular {

class ProposalPublisherImpl;

// This class is currently responsible for 3 things:
//
// 1) Maintaining repositories of ranked Suggestions (stored inside
//    the RankedSuggestionsList class) for both Query and Next proposals.
//  a) Queries are handled by the QueryProcessor. QueryProcessor executes the
//     queries and stores their results. QueryProcessor only executes one query
//     at a time and stores results for only the last query.
//
//  b) Next suggestions are issued by ProposalPublishers through the
//     Propose method, and can be issued at any time. The NextProcessor
//     handles all processing and notification of these proposals and stores
//     them.
//
//  c) New next proposals are also considered for interruption. The
//     InterruptionProcessor examines proposals, decides whether they
//     should interruption, and, if so, makes further decisions about
//     when and how those interruptions should take place.
//
// 2) Storing the FIDL bindings for QueryHandlers and ProposalPublishers.
//
//  a) ProposalPublishers (for Next Suggestions) can be registered via the
//     RegisterProposalPublisher method.
//
//  b) QueryHandlers are currently registered through the
//     RegisterQueryHandler method.
//
// 3) Acts as a SuggestionProvider for those wishing to subscribe to
//    Suggestions.
class SuggestionEngineImpl : public ContextListener,
                             public SuggestionEngine,
                             public SuggestionProvider {
 public:
  SuggestionEngineImpl(media::AudioServerPtr audio_server);
  ~SuggestionEngineImpl();

  fxl::WeakPtr<SuggestionDebugImpl> debug();

  // TODO(andrewosh): The following two methods should be removed. New
  // ProposalPublishers should be created whenever they're requested, and they
  // should be erased automatically when the client disconnects (they should be
  // stored in a BindingSet with an error handler that performs removal).
  void RemoveSourceClient(const std::string& component_url) {
    proposal_publishers_.erase(component_url);
  }

  void Connect(fidl::InterfaceRequest<SuggestionEngine> request);

  void Connect(fidl::InterfaceRequest<SuggestionDebug> request);

  void Connect(fidl::InterfaceRequest<SuggestionProvider> request);

  // Should only be called from ProposalPublisherImpl.
  void AddNextProposal(ProposalPublisherImpl* source, Proposal proposal);

  // Should only be called from ProposalPublisherImpl.
  void RemoveNextProposal(const std::string& component_url,
                          const std::string& proposal_id);

  // |SuggestionProvider|
  void SubscribeToInterruptions(
      fidl::InterfaceHandle<InterruptionListener> listener) override;

  // |SuggestionProvider|
  void SubscribeToNext(fidl::InterfaceHandle<NextListener> listener,
                       int count) override;

  // |SuggestionProvider|
  void Query(fidl::InterfaceHandle<QueryListener> listener, UserInput input,
             int count) override;

  // |SuggestionProvider|
  void RegisterFeedbackListener(
      fidl::InterfaceHandle<FeedbackListener> speech_listener) override;

  // When a user interacts with a Suggestion, the suggestion engine will be
  // notified of consumed suggestion's ID. With this, we will do two things:
  //
  // 1) Perform the Action contained in the Suggestion
  //    (suggestion->proposal.on_selected)
  //
  //    Action handling should be extracted into separate classes to simplify
  //    SuggestionEngineImpl (i.e. an ActionManager which delegates action
  //    execution to ActionHandlers based on the Action's tag).
  //
  // 2) Remove consumed Suggestion from the next_suggestions_ repository,
  //    if it came from there.  Clear the ask_suggestions_ repository if
  //    it came from there.
  //
  // |SuggestionProvider|
  void NotifyInteraction(fidl::StringPtr suggestion_uuid,
                         Interaction interaction) override;

  // |SuggestionEngine|
  void RegisterProposalPublisher(
      fidl::StringPtr url,
      fidl::InterfaceRequest<ProposalPublisher> publisher) override;

  // |SuggestionEngine|
  void RegisterQueryHandler(
      fidl::StringPtr url,
      fidl::InterfaceHandle<QueryHandler> query_handler) override;

  // |SuggestionEngine|
  void Initialize(
      fidl::InterfaceHandle<fuchsia::modular::StoryProvider> story_provider,
      fidl::InterfaceHandle<fuchsia::modular::FocusProvider> focus_provider,
      fidl::InterfaceHandle<ContextWriter> context_writer,
      fidl::InterfaceHandle<ContextReader> context_reader) override;

  void Terminate(std::function<void()> done) { done(); }

 private:
  friend class NextProcessor;
  friend class QueryProcessor;

  // TODO(andrewosh): Performing actions should be handled by a separate
  // interface that's passed to the SuggestionEngineImpl.
  //
  // |actions| are the actions to perform.
  // |listener| is the listener to be notified when the actions have been
  // performed.
  // |proposal_id| is the id of the proposal that was the source of the actions.
  // |story_name| is the external id for the story that the client chooses.
  // |source_url| is the url of the source of the proposal containing
  // the provided actions.
  void PerformActions(fidl::VectorPtr<Action> actions,
                      fidl::InterfaceHandle<ProposalListener> listener,
                      const std::string& proposal_id,
                      const std::string& story_name,
                      const std::string& source_url,
                      SuggestionDisplay suggestion_display);

  void ExecuteActions(fidl::VectorPtr<Action> actions,
                      fidl::InterfaceHandle<ProposalListener> listener,
                      const std::string& proposal_id,
                      SuggestionDisplay suggestion_display,
                      const std::string& override_story_id);

  // Performs an action that creates a story.
  //
  // |proposal| is the proposal that initiated the action, and its listener will
  // be notified with the created story id.
  void PerformCreateStoryAction(
      const Action& action, fidl::InterfaceHandle<ProposalListener> listener,
      const std::string& proposal_id, SuggestionDisplay suggestion_display);

  void PerformFocusStoryAction(const Action& action,
                               const std::string& override_story_id);

  void PerformAddModuleAction(const Action& action,
                              const std::string& override_story_id);

  void PerformSetLinkValueAction(const Action& action,
                                 const std::string& override_story_id);

  void PerformQueryAction(const Action& action);

  void PerformCustomAction(Action* action);

  void RegisterRankingFeatures();

  // |ContextListener|
  void OnContextUpdate(ContextUpdate update) override;

  std::string StoryIdFromName(const std::string& source_url,
                              const std::string& story_name);

  fidl::BindingSet<SuggestionEngine> bindings_;
  fidl::BindingSet<SuggestionProvider> suggestion_provider_bindings_;
  fidl::BindingSet<SuggestionDebug> debug_bindings_;

  // Maps a story name (external id) to its framework id.
  // TODO(miguelfrde): move this into the framework.
  std::map<std::string, std::string> story_name_mapping_;

  // Both story_provider_ and focus_provider_ptr are used exclusively during
  // Action execution (in the PerformActions call inside NotifyInteraction).
  //
  // These are required to create new Stories and interact with the current
  // Story.
  fuchsia::modular::StoryProviderPtr story_provider_;
  fidl::InterfacePtr<fuchsia::modular::FocusProvider> focus_provider_ptr_;

  // Watches for changes in StoryInfo from the StoryProvider, acts as a filter
  // for Proposals on all channels, and notifies when there are changes so that
  // we can re-filter Proposals.
  //
  // Initialized late in Initialize().
  std::unique_ptr<TimelineStoriesWatcher> timeline_stories_watcher_;

  // The debugging interface for all Suggestions.
  std::shared_ptr<SuggestionDebugImpl> debug_;

  // TODO(thatguy): All Channels also get a ReevaluateFilters method, which
  // would remove Suggestions that are now filtered or add
  // new ones that are no longer filtered.

  // next and interruptions share the same backing
  NextProcessor next_processor_;

  // query execution and processing
  QueryProcessor query_processor_;

  std::map<std::string, std::shared_ptr<RankingFeature>> ranking_features;

  // The ProposalPublishers that have registered with the SuggestionEngine.
  std::map<std::string, std::unique_ptr<ProposalPublisherImpl>>
      proposal_publishers_;

  // The context reader that is used to rank suggestions using the current
  // context.
  ContextReaderPtr context_reader_;
  fidl::Binding<ContextListener> context_listener_binding_;

  // Used to jackpot a suggestion when a QueryAction is executed.
  AutoSelectFirstQueryListener auto_select_first_query_listener_;
  fidl::Binding<QueryListener> auto_select_first_query_listener_binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SuggestionEngineImpl);
};

}  // namespace modular
}  // namespace fuchsia

#endif  // PERIDOT_BIN_SUGGESTION_ENGINE_SUGGESTION_ENGINE_IMPL_H_
