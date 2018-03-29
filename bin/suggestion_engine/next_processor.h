// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SUGGESTION_ENGINE_NEXT_PROCESSOR_H_
#define PERIDOT_BIN_SUGGESTION_ENGINE_NEXT_PROCESSOR_H_

#include <vector>

#include <fuchsia/cpp/modular.h>
#include "lib/fidl/cpp/binding.h"
#include <fuchsia/cpp/modular.h>

#include "peridot/bin/suggestion_engine/debug.h"
#include "peridot/bin/suggestion_engine/interruptions_processor.h"
#include "peridot/bin/suggestion_engine/proposal_publisher_impl.h"
#include "peridot/bin/suggestion_engine/ranked_suggestion.h"
#include "peridot/bin/suggestion_engine/ranked_suggestions_list.h"
#include "peridot/bin/suggestion_engine/suggestion_prototype.h"

namespace modular {

class ProposalPublisherImpl;
class SuggestionEngineImpl;

// The NextProcessor manages all contextual proposals for the suggestion
// engine.
class NextProcessor {
 public:
  NextProcessor(std::shared_ptr<SuggestionDebugImpl> debug);
  virtual ~NextProcessor();

  void RegisterListener(fidl::InterfaceHandle<NextListener> listener,
                        size_t max_results);

  void RegisterInterruptionListener(
      fidl::InterfaceHandle<InterruptionListener> listener);

  // Add and remove proposals
  void AddProposal(const std::string& component_url, Proposal proposal);
  void RemoveProposal(const std::string& component_url,
                      const std::string& proposal_id);

  // Add feature for ranking.
  void AddRankingFeature(
      double weight, std::shared_ptr<RankingFeature> ranking_feature);

  // Gets a suggestion stored in the processor.
  RankedSuggestion* GetSuggestion(const std::string& suggestion_id) const;

  // Reranks suggestions if dirty and updates listeners
  void UpdateRanking();

  // Notify the listeners of new suggestions
  void NotifyAllOfResults();

  // Notifies the listeners that the processing state has changed.
  void NotifyOfProcessingChange(bool processing);

 private:
  // (proposer ID, proposal ID) => suggestion prototype
  using SuggestionPrototypeMap = std::map<std::pair<std::string, std::string>,
                                          std::unique_ptr<SuggestionPrototype>>;

  void NotifyOfResults(const NextListenerPtr& listener, size_t max_results);

  void RemoveProposalFromList(const std::string& component_url,
                              const std::string& proposal_id);

  InterruptionsProcessor interruptions_processor_;
  RankedSuggestionsList suggestions_;
  std::shared_ptr<SuggestionDebugImpl> debug_;
  SuggestionPrototypeMap prototypes_;
  bool dirty_;
  bool processing_;

  std::vector<std::pair<NextListenerPtr, size_t>> listeners_;
};

}  // namespace modular

#endif  // PERIDOT_BIN_SUGGESTION_ENGINE_NEXT_PROCESSOR_H_
