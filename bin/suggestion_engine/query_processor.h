// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SUGGESTION_ENGINE_QUERY_PROCESSOR_H_
#define PERIDOT_BIN_SUGGESTION_ENGINE_QUERY_PROCESSOR_H_

#include <set>

#include <fuchsia/media/cpp/fidl.h>
#include <fuchsia/modular/cpp/fidl.h>
#include <lib/fxl/memory/weak_ptr.h>

#include "peridot/bin/suggestion_engine/media_player.h"
#include "peridot/bin/suggestion_engine/query_runner.h"
#include "peridot/bin/suggestion_engine/rankers/ranker.h"
#include "peridot/bin/suggestion_engine/suggestion_prototype.h"
#include "peridot/lib/util/idle_waiter.h"

namespace modular {

class SuggestionEngineImpl;

// The query processor handles the pull-based query suggestion process,
// including requesting suggestions from QueryHandlers, collating and
// ranking those suggestions, and then providing them to the user.
class QueryProcessor {
 public:
  QueryProcessor(fuchsia::media::AudioPtr audio,
                 std::shared_ptr<SuggestionDebugImpl> debug);
  ~QueryProcessor();

  void Initialize(
      fidl::InterfaceHandle<fuchsia::modular::ContextWriter> context_writer);

  // Runs a query and notifies listener with results from it with the given
  // input and providing 'count' results. It also caches all query results for
  // future fetching using GetSuggestion(). Each time ExecuteQuery is called,
  // suggestions from the previous query are cleared by calling
  // CleanUpPreviousQuery() internally.
  void ExecuteQuery(
      fuchsia::modular::UserInput input, int count,
      fidl::InterfaceHandle<fuchsia::modular::QueryListener> listener);

  // Registers a feedback listener for speech status updates.
  void RegisterFeedbackListener(
      fidl::InterfaceHandle<fuchsia::modular::FeedbackListener>
          speech_listener);

  // Registers a handler that will be notified when a new query comes for its
  // fullfillment.
  void RegisterQueryHandler(
      fidl::StringPtr url,
      fidl::InterfaceHandle<fuchsia::modular::QueryHandler> query_handler);

  void SetFilters(
      std::vector<std::unique_ptr<SuggestionActiveFilter>>&& active_filters,
      std::vector<std::unique_ptr<SuggestionPassiveFilter>>&& passive_filters);

  void SetRanker(std::unique_ptr<Ranker> ranker);

  // Returns a query suggestion with the given id.
  // While a query is being executed or if no query has been executed, nullptr
  // will be returned for any |suggestion_id|. If |suggestion_id| is not in the
  // set of results given to the |listener| provided to the most recent
  // invocation of ExecuteQuery(), return nullptr.
  RankedSuggestion* GetSuggestion(const std::string& suggestion_id) const;

  // Cleans up all resources associated with a query, including clearing
  // the previous ask suggestions, closing any still open SuggestionListeners,
  // etc.
  void CleanUpPreviousQuery();

 private:
  // (proposer ID, proposal ID) => suggestion prototype
  using SuggestionPrototypeMap = std::map<std::pair<std::string, std::string>,
                                          std::unique_ptr<SuggestionPrototype>>;

  void AddProposal(const std::string& source_url,
                   fuchsia::modular::Proposal proposal);

  void NotifySpeechListeners(fuchsia::modular::SpeechStatus status);

  void OnQueryResponse(fuchsia::modular::UserInput input,
                       const std::string& handler_url,
                       fuchsia::modular::QueryResponse response);

  void OnQueryEndRequest(fuchsia::modular::UserInput input);

  void NotifyOfResults();

  std::shared_ptr<SuggestionDebugImpl> debug_;
  MediaPlayer media_player_;
  RankedSuggestionsList suggestions_;
  SuggestionPrototypeMap query_prototypes_;
  fidl::InterfacePtrSet<fuchsia::modular::FeedbackListener> speech_listeners_;

  // Unique ptr for the query runner executing the query being processed.
  std::unique_ptr<QueryRunner> active_query_;

  // The fuchsia::modular::ContextWriter that publishes the current user query
  // to the fuchsia::modular::ContextEngine.
  fuchsia::modular::ContextWriterPtr context_writer_;

  // The set of all QueryHandlers that have been registered mapped to their
  // URLs (stored as strings).
  std::vector<QueryHandlerRecord> query_handlers_;

  // When multiple handlers want to play media as part of their responses, we
  // only want to allow one of them to do so. For lack of a better policy, we
  // play the first one we encounter.
  bool has_audio_response_;

  util::IdleWaiter::ActivityToken activity_;
};

}  // namespace modular

#endif  // PERIDOT_BIN_SUGGESTION_ENGINE_QUERY_PROCESSOR_H_
