// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/maxwell/services/suggestion/suggestion_provider.fidl.h"
#include "apps/maxwell/src/suggestion_engine/agent_suggestion_record.h"
#include "lib/fidl/cpp/bindings/binding.h"

namespace maxwell {
namespace suggestion {

// Manages a single "Next" suggestion subscriber, translating raw suggestion
// lifecycle events into windowed suggestion lists using a vector of ranked
// suggestions.
//
// TODO(rosswang): Ask is probably the more general case, but we probably want
// a direct propagation channel for agents to be sensitive to Asks (as well as
// an indirect context channel to catch agents that weren't engineered for Ask).
class NextSubscriber : public NextController {
 public:
  static fidl::Binding<NextController>* GetBinding(
      std::unique_ptr<NextSubscriber>* next_subscriber) {
    return &(*next_subscriber)->binding_;
  }

  NextSubscriber(
      const std::vector<std::unique_ptr<RankedSuggestion>>* ranked_suggestions,
      fidl::InterfaceHandle<Listener> listener)
      : binding_(this),
        ranked_suggestions_(ranked_suggestions),
        listener_(ListenerPtr::Create(std::move(listener))) {}

  void Bind(fidl::InterfaceRequest<NextController> request) {
    binding_.Bind(std::move(request));
  }

  void SetResultCount(int32_t count) override;

  void OnAddSuggestion(const RankedSuggestion& ranked_suggestion) {
    if (IncludeSuggestion(ranked_suggestion)) {
      DispatchAdd(ranked_suggestion);

      // Evict if we were already full
      if (IsFull())
        DispatchRemove(*(*ranked_suggestions_)[max_results_]);
    }
  }

  void OnRemoveSuggestion(const RankedSuggestion& ranked_suggestion) {
    if (IncludeSuggestion(ranked_suggestion)) {
      // Shift in if we were full
      if (IsFull())
        DispatchAdd(*(*ranked_suggestions_)[max_results_]);

      DispatchRemove(ranked_suggestion);
    }
  }

 private:
  static SuggestionPtr CreateSuggestion(
      const RankedSuggestion& suggestion_data) {
    auto suggestion = Suggestion::New();
    suggestion->uuid = suggestion_data.prototype->first;
    suggestion->rank = suggestion_data.rank;
    suggestion->display =
        suggestion_data.prototype->second->proposal->display->Clone();
    return suggestion;
  }

  bool IsFull() const {
    return ranked_suggestions_->size() > (size_t)max_results_;
  }

  void DispatchAdd(const RankedSuggestion& ranked_suggestion) {
    fidl::Array<SuggestionPtr> batch;
    batch.push_back(CreateSuggestion(ranked_suggestion));
    listener_->OnAdd(std::move(batch));
  }

  void DispatchRemove(const RankedSuggestion& ranked_suggestion) {
    listener_->OnRemove(ranked_suggestion.prototype->first);
  }

  bool IncludeSuggestion(const RankedSuggestion& suggestion) const;

  fidl::Binding<NextController> binding_;
  // An upper bound on the number of suggestions to offer this subscriber, as
  // given by SetResultCount.
  int32_t max_results_ = 0;
  const std::vector<std::unique_ptr<RankedSuggestion>>* ranked_suggestions_;
  ListenerPtr listener_;
};

}  // namespace suggestion
}  // namespace maxwell
