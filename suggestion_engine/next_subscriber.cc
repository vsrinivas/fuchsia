// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/suggestion_engine/next_subscriber.h"

namespace maxwell {
namespace suggestion_engine {

void NextSubscriber::SetResultCount(int32_t count) {
  if (count < 0)
    count = 0;

  size_t target = std::min((size_t)count, ranked_suggestions_->size());
  size_t prev = std::min((size_t)max_results_, ranked_suggestions_->size());

  if (target != prev) {
    if (target > prev) {
      fidl::Array<SuggestionPtr> delta;
      for (size_t i = prev; i < target; i++) {
        delta.push_back((*ranked_suggestions_)[i]->Clone());
      }
      listener_->OnAdd(std::move(delta));
    } else if (target == 0) {
      listener_->OnRemoveAll();
    } else if (target < prev) {
      for (size_t i = prev - 1; i >= target; i--) {
        listener_->OnRemove((*ranked_suggestions_)[i]->uuid);
      }
    }
  }

  max_results_ = count;
}

// A suggestion should be included if its sorted index (by rank) is less than
// max_results_. We don't have to do a full iteration here since we can just
// compare the rank with the tail for all but the edge case where ranks are
// identical.
bool NextSubscriber::IncludeSuggestion(const Suggestion& suggestion) const {
  if (max_results_ == 0)
    return false;
  if (ranked_suggestions_->size() <= (size_t)max_results_)
    return true;

  float newRank = suggestion.rank;

  const int32_t i = max_results_ - 1;
  auto it = ranked_suggestions_->begin() + i;

  if (newRank > (*it)->rank)
    return false;

  if (newRank < (*it)->rank)
    return true;

  // else we actually have to iterate. Iterate until the rank is less than
  // the new suggestion, at which point we can conclude that the new
  // suggestion has not made it into the window.
  do {
    // Could also compare UUIDs
    if (*it == &suggestion) {
      return true;
    }

    // backwards iteration is inelegant.
    if (it == ranked_suggestions_->begin())
      return false;

    --it;
  } while (newRank == (*it)->rank);

  return false;
}

}  // namespace suggestion_engine
}  // namespace maxwell
