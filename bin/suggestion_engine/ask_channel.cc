// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/suggestion_engine/ask_channel.h"

#include <cassert>

#include "apps/maxwell/src/suggestion_engine/ask_subscriber.h"

namespace maxwell {
namespace suggestion {

// rank assigned to excluded suggestions, to simplify differentiated logic.
// Eventually, we will likely use a threshold instead.
constexpr float kExcludeRank = std::numeric_limits<float>::infinity();

bool AskChannel::IncludeSuggestion(const SuggestionPrototype* prototype) const {
  // TODO(rosswang): rank; do exact match for now
  return query_.empty() ||
         prototype->second->proposal->display->headline == query_;
}

RankedSuggestion* AskChannel::OnAddSuggestion(
    const SuggestionPrototype* prototype) {
  // TODO(rosswang): rank; do exact match for now
  if (IncludeSuggestion(prototype)) {
    const float rank = next_rank();

    auto& new_entry = include_.emplace_back(new RankedSuggestion());
    new_entry->rank = rank;
    new_entry->prototype = prototype;

    subscriber_.OnAddSuggestion(*new_entry);

    return new_entry.get();
  } else {
    auto& new_entry = exclude_.emplace(prototype->first, new RankedSuggestion())
                          .first->second;
    new_entry->rank = kExcludeRank;
    new_entry->prototype = prototype;

    return new_entry.get();
  }
}

auto find(AskChannel::RankedSuggestions* suggestions,
          const RankedSuggestion* suggestion) {
  auto it = std::lower_bound(
      suggestions->begin(), suggestions->end(), *suggestion,
      [](const std::unique_ptr<RankedSuggestion>& a,
         const RankedSuggestion& b) { return a->rank < b.rank; });
  assert(it->get() == suggestion);
  return it;
}

void AskChannel::OnChangeSuggestion(RankedSuggestion* ranked_suggestion) {
  // TODO(rosswang): add a change specialization to remove jank
  // When an item is removed and then added using these methods, another item is
  // temporarily shifted in and back out while this goes on. This should not
  // happen.
  if (ranked_suggestion->rank != kExcludeRank) {
    // previously included
    subscriber_.OnRemoveSuggestion(*ranked_suggestion);

    if (IncludeSuggestion(ranked_suggestion->prototype)) {
      subscriber_.OnAddSuggestion(*ranked_suggestion);
    } else {
      auto it = find(&include_, ranked_suggestion);
      ranked_suggestion->rank = kExcludeRank;
      exclude_.emplace(ranked_suggestion->prototype->first, std::move(*it));
      include_.erase(it);
    }
  } else {
    // previously excluded
    if (IncludeSuggestion(ranked_suggestion->prototype)) {
      auto it = exclude_.find(ranked_suggestion->prototype->first);
      ranked_suggestion->rank = next_rank();
      include_.emplace_back(std::move(it->second));
      exclude_.erase(it);
      subscriber_.OnAddSuggestion(*ranked_suggestion);
    }
    // else no action required
  }
}

void AskChannel::OnRemoveSuggestion(const RankedSuggestion* ranked_suggestion) {
  if (ranked_suggestion->rank == kExcludeRank) {
    exclude_.erase(ranked_suggestion->prototype->first);
  } else {
    subscriber_.OnRemoveSuggestion(*ranked_suggestion);
    include_.erase(find(&include_, ranked_suggestion));
  }
}

const AskChannel::RankedSuggestions* AskChannel::ranked_suggestions() const {
  return &include_;
}

void AskChannel::SetQuery(const std::string& query) {
  if (query_ != query) {
    query_ = query;

    AskChannel::RankedSuggestions to_exclude;
    // Remove included that are now excluded
    for (auto it = include_.begin(); it != include_.end();) {
      if (!IncludeSuggestion((*it)->prototype)) {
        to_exclude.emplace_back(std::move(*it));
        it = include_.erase(it);
      } else {
        ++it;
      }
    }
    // Add excluded that are now included
    for (auto it = exclude_.begin(); it != exclude_.end();) {
      if (IncludeSuggestion(it->second->prototype)) {
        it->second->rank = next_rank();
        include_.emplace_back(std::move(it->second));
        it = exclude_.erase(it);
      } else {
        ++it;
      }
    }
    // Update excluded
    for (auto& ranked_suggestion : to_exclude) {
      ranked_suggestion->rank = kExcludeRank;
      exclude_.emplace(ranked_suggestion->prototype->first,
                       std::move(ranked_suggestion));
    }

    subscriber_.Invalidate();
  }
}

}  // namespace suggestion
}  // namespace maxwell
