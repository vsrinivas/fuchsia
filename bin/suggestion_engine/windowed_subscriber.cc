// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/windowed_subscriber.h"

namespace maxwell {

void WindowedSuggestionSubscriber::OnSubscribe() {
  auto& suggestions = ranked_suggestions_->Get();
  for (size_t i = 0; i < (size_t)max_results_ && i < suggestions.size(); i++) {
    DispatchAdd(*suggestions[i]);
  }
}

void WindowedSuggestionSubscriber::OnAddSuggestion(
    const RankedSuggestion& ranked_suggestion) {
  if (IncludeSuggestion(ranked_suggestion)) {
    DispatchAdd(ranked_suggestion);

    // Evict if we were already full
    if (IsFull())
      DispatchRemove(*ranked_suggestions_->Get()[max_results_]);
  }
}

void WindowedSuggestionSubscriber::OnRemoveSuggestion(
    const RankedSuggestion& ranked_suggestion) {
  if (IncludeSuggestion(ranked_suggestion)) {
    // Shift in if we were full
    if (IsFull())
      DispatchAdd(*ranked_suggestions_->Get()[max_results_]);

    DispatchRemove(ranked_suggestion);
  }
}

void WindowedSuggestionSubscriber::Invalidate() {
  listener()->OnRemoveAll();

  const auto& suggestion_vector = ranked_suggestions_->Get();

  fidl::Array<SuggestionPtr> window;
  for (int32_t i = 0;
       i < max_results_ && (unsigned)i < suggestion_vector.size(); i++) {
    window.push_back(CreateSuggestion(*suggestion_vector[i]));
  }

  if (window)  // after OnRemoveAll, no point in adding if no window
    listener()->OnAdd(std::move(window));
}

void WindowedSuggestionSubscriber::OnProcessingChange(bool processing) {
  listener()->OnProcessingChange(processing);
}

// A suggestion should be included if its sorted index (by rank) is less than
// max_results_. We don't have to do a full iteration here since we can just
// compare the rank with the tail for all but the edge case where ranks are
// identical.
//
// The mutable content of the RankedSuggestion given here is not used; only the
// rank and pointer address or ID are considered.
bool WindowedSuggestionSubscriber::IncludeSuggestion(
    const RankedSuggestion& ranked_suggestion) const {
  if (max_results_ == 0)
    return false;
  if (!IsFull())
    return true;

  const auto& suggestion_vector = ranked_suggestions_->Get();

  double newRank = ranked_suggestion.confidence;

  const int32_t i = max_results_ - 1;
  auto it = suggestion_vector.begin() + i;

  if (newRank > (*it)->confidence)
    return true;

  // Else we might actually have to iterate. Iterate until the rank is less than
  // the new suggestion, at which point we can conclude that the new suggestion
  // has not made it into the window.
  while (newRank == (*it)->confidence) {
    // Could also compare UUIDs
    if ((*it)->prototype == ranked_suggestion.prototype) {
      return true;
    }

    // backwards iteration is inelegant.
    if (it == suggestion_vector.begin())
      return false;

    --it;
  }

  return false;
}

}  // namespace maxwell
