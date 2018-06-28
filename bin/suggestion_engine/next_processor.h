// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SUGGESTION_ENGINE_NEXT_PROCESSOR_H_
#define PERIDOT_BIN_SUGGESTION_ENGINE_NEXT_PROCESSOR_H_

#include <vector>

#include <fuchsia/modular/cpp/fidl.h>

#include "lib/fidl/cpp/binding.h"
#include "peridot/bin/suggestion_engine/debug.h"
#include "peridot/bin/suggestion_engine/decision_policies/decision_policy.h"
#include "peridot/bin/suggestion_engine/filters/suggestion_active_filter.h"
#include "peridot/bin/suggestion_engine/filters/suggestion_passive_filter.h"
#include "peridot/bin/suggestion_engine/interruptions_processor.h"
#include "peridot/bin/suggestion_engine/proposal_publisher_impl.h"
#include "peridot/bin/suggestion_engine/ranked_suggestion.h"
#include "peridot/bin/suggestion_engine/ranked_suggestions_list.h"
#include "peridot/bin/suggestion_engine/rankers/ranker.h"
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

  void RegisterListener(
      fidl::InterfaceHandle<fuchsia::modular::NextListener> listener,
      size_t max_results);

  void RegisterInterruptionListener(
      fidl::InterfaceHandle<fuchsia::modular::InterruptionListener> listener);

  // Add and remove proposals
  void AddProposal(const std::string& component_url,
                   const std::string& story_id,
                   fuchsia::modular::Proposal proposal);
  void RemoveProposal(const std::string& component_url,
                      const std::string& proposal_id);

  void SetActiveFilters(
      std::vector<std::unique_ptr<SuggestionActiveFilter>>&& active_filters);

  void SetPassiveFilters(
      std::vector<std::unique_ptr<SuggestionPassiveFilter>>&& passive_filters);

  void SetRanker(std::unique_ptr<Ranker> ranker);

  void SetInterruptionDecisionPolicy(std::unique_ptr<DecisionPolicy> ranker);

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

  void NotifyOfResults(const fuchsia::modular::NextListenerPtr& listener,
                       size_t max_results);

  void RemoveProposalFromList(const std::string& component_url,
                              const std::string& proposal_id);

  InterruptionsProcessor interruptions_processor_;
  RankedSuggestionsList suggestions_;
  std::shared_ptr<SuggestionDebugImpl> debug_;
  SuggestionPrototypeMap prototypes_;
  bool processing_;

  std::vector<std::pair<fuchsia::modular::NextListenerPtr, size_t>> listeners_;
};

}  // namespace modular

#endif  // PERIDOT_BIN_SUGGESTION_ENGINE_NEXT_PROCESSOR_H_
