// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SUGGESTION_ENGINE_SUGGESTION_ENGINE_IMPL_H_
#define PERIDOT_BIN_SUGGESTION_ENGINE_SUGGESTION_ENGINE_IMPL_H_

#include <map>
#include <string>
#include <vector>

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/async/cpp/future.h>
#include <lib/fidl/cpp/interface_ptr_set.h>
#include <src/lib/fxl/memory/weak_ptr.h>

#include "peridot/bin/suggestion_engine/debug.h"
#include "peridot/bin/suggestion_engine/interruptions_processor.h"
#include "peridot/bin/suggestion_engine/navigation_processor.h"
#include "peridot/bin/suggestion_engine/next_processor.h"
#include "peridot/bin/suggestion_engine/proposal_publisher_impl.h"
#include "peridot/bin/suggestion_engine/query_processor.h"
#include "peridot/bin/suggestion_engine/ranked_suggestions_list.h"
#include "peridot/lib/bound_set/bound_set.h"

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
// 3) Acts as a fuchsia::modular::SuggestionProvider for those wishing to
// subscribe to
//    Suggestions.
class SuggestionEngineImpl : public fuchsia::modular::ContextListener,
                             public fuchsia::modular::SuggestionEngine,
                             public fuchsia::modular::SuggestionProvider {
 public:
  SuggestionEngineImpl(fuchsia::modular::ContextReaderPtr context_reader,
                       fuchsia::modular::PuppetMasterPtr puppet_master);
  ~SuggestionEngineImpl() override;

  fxl::WeakPtr<SuggestionDebugImpl> debug();

  // TODO(andrewosh): The following two methods should be removed. New
  // ProposalPublishers should be created whenever they're requested, and they
  // should be erased automatically when the client disconnects (they should be
  // stored in a BindingSet with an error handler that performs removal).
  void RemoveSourceClient(const std::string& component_url) {
    proposal_publishers_.erase(component_url);
  }

  void Connect(
      fidl::InterfaceRequest<fuchsia::modular::SuggestionEngine> request);

  void Connect(
      fidl::InterfaceRequest<fuchsia::modular::SuggestionDebug> request);

  void Connect(
      fidl::InterfaceRequest<fuchsia::modular::SuggestionProvider> request);

  // Should only be called from ProposalPublisherImpl.
  void AddNextProposal(ProposalPublisherImpl* source,
                       fuchsia::modular::Proposal proposal);

  // Should only be called from ProposalPublisherImpl.
  void RemoveNextProposal(const std::string& component_url,
                          const std::string& proposal_id);

  // Should only be called from ProposalPublisherImpl.
  void ProposeNavigation(fuchsia::modular::NavigationAction navigation);

  // |fuchsia::modular::SuggestionProvider|
  void SubscribeToInterruptions(
      fidl::InterfaceHandle<fuchsia::modular::InterruptionListener> listener)
      override;

  // |fuchsia::modular::SuggestionProvider|
  void SubscribeToNavigation(
      fidl::InterfaceHandle<fuchsia::modular::NavigationListener> listener)
      override;

  // |fuchsia::modular::SuggestionProvider|
  void SubscribeToNext(
      fidl::InterfaceHandle<fuchsia::modular::NextListener> listener,
      int count) override;

  // |fuchsia::modular::SuggestionProvider|
  void Query(fidl::InterfaceHandle<fuchsia::modular::QueryListener> listener,
             fuchsia::modular::UserInput input, int count) override;

  // When a user interacts with a fuchsia::modular::Suggestion, the suggestion
  // engine will be notified of consumed suggestion's ID. With this, we will do
  // two things:
  //
  // 1) Perform the fuchsia::modular::Action contained in the
  // fuchsia::modular::Suggestion
  //    (suggestion->proposal.on_selected)
  //
  //    fuchsia::modular::Action handling should be extracted into separate
  //    classes to simplify SuggestionEngineImpl (i.e. an ActionManager which
  //    delegates action execution to ActionHandlers based on the
  //    fuchsia::modular::Action's tag).
  //
  // 2) Remove consumed fuchsia::modular::Suggestion from the next_suggestions_
  // repository,
  //    if it came from there.  Clear the ask_suggestions_ repository if
  //    it came from there.
  //
  // |fuchsia::modular::SuggestionProvider|
  void NotifyInteraction(std::string suggestion_uuid,
                         fuchsia::modular::Interaction interaction) override;

  // |fuchsia::modular::SuggestionEngine|
  void RegisterProposalPublisher(
      std::string url,
      fidl::InterfaceRequest<fuchsia::modular::ProposalPublisher> publisher)
      override;

  // |fuchsia::modular::SuggestionEngine|
  void RegisterQueryHandler(
      std::string url, fidl::InterfaceHandle<fuchsia::modular::QueryHandler>
                           query_handler_handle) override;

  void Terminate(fit::function<void()> done) { done(); }

 private:
  friend class NavigationProcessor;
  friend class NextProcessor;
  friend class QueryProcessor;

  // Used by AddNextProposal to create a kind-of-proto-story and pre execute
  // actions when |proposal.preload| is true.
  void AddProposalWithRichSuggestion(ProposalPublisherImpl* source,
                                     fuchsia::modular::Proposal proposal);

  void RegisterRankingFeatures();

  // |fuchsia::modular::ContextListener|
  void OnContextUpdate(fuchsia::modular::ContextUpdate update) override;

  // Returns true iff the component at |component_url| is allowed to make rich
  // suggestions (i.e. pre-load stories to be displayed as suggestions).
  bool ComponentCanUseRichSuggestions(const std::string& component_url);

  // Executes the Interaction::SELECTED operation. If a |preloaded_story_id| is
  // provided, it will be promoted. Otherwise the actions will be executed.
  // Also notifies of OnProposalAccepted on the |proposal.listener|.
  // If |suggestion_in_ask| is true means that the proposal belongs to a query
  // suggestion. If false, to a next suggestion. This is information is used to
  // know from which list to delete.
  void HandleSelectedInteraction(const std::string& component_url,
                                 const std::string& preloaded_story_id,
                                 fuchsia::modular::Proposal& proposal,
                                 fuchsia::modular::ProposalListenerPtr listener,
                                 bool suggestion_in_ask);

  fidl::BindingSet<fuchsia::modular::SuggestionEngine> bindings_;
  fidl::BindingSet<fuchsia::modular::SuggestionProvider>
      suggestion_provider_bindings_;
  fidl::BindingSet<fuchsia::modular::SuggestionDebug> debug_bindings_;

  // The debugging interface for all Suggestions.
  std::shared_ptr<SuggestionDebugImpl> debug_;

  // TODO(thatguy): All Channels also get a ReevaluateFilters method, which
  // would remove Suggestions that are now filtered or add
  // new ones that are no longer filtered.

  // next and interruptions share the same backing
  NextProcessor next_processor_;

  // query execution and processing
  QueryProcessor query_processor_;

  // executes navigation actions
  NavigationProcessor navigation_processor_;

  std::map<std::string, std::shared_ptr<RankingFeature>> ranking_features;

  // The ProposalPublishers that have registered with the
  // fuchsia::modular::SuggestionEngine.
  std::map<std::string, std::unique_ptr<ProposalPublisherImpl>>
      proposal_publishers_;

  // The context reader that is used to rank suggestions using the current
  // context.
  fuchsia::modular::ContextReaderPtr context_reader_;

  // The puppet master connection that is used to execute actions.
  fuchsia::modular::PuppetMasterPtr puppet_master_;

  fidl::Binding<fuchsia::modular::ContextListener> context_listener_binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SuggestionEngineImpl);
};

}  // namespace modular

#endif  // PERIDOT_BIN_SUGGESTION_ENGINE_SUGGESTION_ENGINE_IMPL_H_
