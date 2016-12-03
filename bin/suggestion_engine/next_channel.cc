// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/suggestion_engine/next_channel.h"

namespace maxwell {

RankedSuggestion* NextChannel::OnAddSuggestion(
    const SuggestionPrototype* prototype) {
  // TODO(rosswang): remove existing if they match filter
  if (filter_ && !filter_(*prototype->proposal)) {
    FTL_LOG(INFO) << "Filtering " << short_proposal_str(*prototype)
                  << " in Next";
    return nullptr;
  }

  // TODO(rosswang): rank
  const float next_rank =
      ranked_suggestions_.empty() ? 0 : ranked_suggestions_.back()->rank + 1;

  auto& new_entry = ranked_suggestions_.emplace_back(new RankedSuggestion());
  new_entry->prototype = prototype;
  new_entry->rank = next_rank;

  DispatchOnAddSuggestion(*new_entry);

  return new_entry.get();
}

void NextChannel::OnChangeSuggestion(RankedSuggestion* ranked_suggestion) {
  DispatchOnRemoveSuggestion(*ranked_suggestion);
  // TODO(rosswang): rerank
  DispatchOnAddSuggestion(*ranked_suggestion);
}

void NextChannel::OnRemoveSuggestion(
    const RankedSuggestion* ranked_suggestion) {
  DispatchOnRemoveSuggestion(*ranked_suggestion);

  auto it = std::lower_bound(
      ranked_suggestions_.begin(), ranked_suggestions_.end(),
      *ranked_suggestion,
      [](const std::unique_ptr<RankedSuggestion>& a,
         const RankedSuggestion& b) { return a->rank < b.rank; });

  FTL_CHECK(it->get() == ranked_suggestion);

  ranked_suggestions_.erase(it);
}

const NextChannel::RankedSuggestions* NextChannel::ranked_suggestions() const {
  return &ranked_suggestions_;
}

}  // namespace maxwell
