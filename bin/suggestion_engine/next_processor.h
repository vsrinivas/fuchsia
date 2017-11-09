// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SUGGESTION_ENGINE_NEXT_PROCESSOR_H_
#define PERIDOT_BIN_SUGGESTION_ENGINE_NEXT_PROCESSOR_H_

#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/suggestion/fidl/suggestion_provider.fidl.h"
#include "peridot/bin/suggestion_engine/proposal_publisher_impl.h"
#include "peridot/bin/suggestion_engine/ranked_suggestion.h"
#include "peridot/bin/suggestion_engine/ranked_suggestions_list.h"
#include "peridot/bin/suggestion_engine/suggestion_engine_impl.h"
#include "peridot/bin/suggestion_engine/suggestion_prototype.h"

#include <vector>

namespace maxwell {

class ProposalPublisherImpl;
class SuggestionEngineImpl;

// The NextProcessor manages all contextual proposals for the suggestion
// engine.
class NextProcessor {
 public:
  NextProcessor(SuggestionEngineImpl* engine);
  virtual ~NextProcessor();

  void RegisterListener(fidl::InterfaceHandle<NextListener> listener,
                        size_t max_results);

  // Add and remove proposals
  void AddProposal(const std::string& component_url, ProposalPtr proposal);
  void RemoveProposal(const std::string& component_url,
                      const std::string& proposal_id);

  // Reranks suggestions if dirty and updates listeners
  void Validate();

  // Notify the listeners of new suggestions
  void NotifyAllOfResults();

  // Notifies the listeners that the processing state has changed.
  void NotifyOfProcessingChange(bool processing);

 private:
  void NotifyOfResults(const NextListenerPtr& listener, size_t max_results);

  SuggestionEngineImpl* const engine_;
  bool dirty_;
  bool processing_;

  std::vector<std::pair<NextListenerPtr, size_t>> listeners_;
};

}  // namespace maxwell

#endif  // PERIDOT_BIN_SUGGESTION_ENGINE_NEXT_PROCESSOR_H_
