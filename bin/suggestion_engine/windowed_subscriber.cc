// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/windowed_subscriber.h"

namespace maxwell {

void WindowedSuggestionSubscriber::SetResultCount(int32_t count) {
  FXL_LOG(INFO) << "WindowedSuggestionSubscriber::SetResultCount(" << count
                << ")";
  if (count < 0)
    count = 0;

  const auto& suggestion_vector = ranked_suggestions_->Get();

  size_t target = std::min((size_t)count, suggestion_vector.size());
  size_t prev = std::min((size_t)max_results_, suggestion_vector.size());

  if (target != prev) {
    if (target > prev) {
      fidl::Array<SuggestionPtr> delta;
      for (size_t i = prev; i < target; i++) {
        delta.push_back(CreateSuggestion(*suggestion_vector[i]));
      }
      listener()->OnAdd(std::move(delta));
    } else if (target == 0) {
      listener()->OnRemoveAll();
    } else if (target < prev) {
      for (size_t i = prev - 1; i >= target; i--) {
        listener()->OnRemove(suggestion_vector[i]->prototype->suggestion_id);
      }
    }
  }

  max_results_ = count;
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

  float newRank = ranked_suggestion.rank;

  const int32_t i = max_results_ - 1;
  auto it = suggestion_vector.begin() + i;

  if (newRank < (*it)->rank)
    return true;

  // Else we might actually have to iterate. Iterate until the rank is less than
  // the new suggestion, at which point we can conclude that the new suggestion
  // has not made it into the window.
  while (newRank == (*it)->rank) {
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
