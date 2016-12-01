// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/suggestion_engine/ask_channel.h"

#include <utility>

#include "apps/maxwell/src/suggestion_engine/ask_subscriber.h"
#include "apps/maxwell/src/suggestion_engine/repo.h"

namespace maxwell {

namespace {

// rank assigned to excluded suggestions, to simplify differentiated logic.
// Eventually, we will likely use a threshold instead.
constexpr float kExcludeRank = std::numeric_limits<float>::infinity();

auto find_for_insert(AskChannel::RankedSuggestions* suggestions, float rank) {
  return std::upper_bound(
      suggestions->begin(), suggestions->end(), rank,
      [](float rank,
         const std::unique_ptr<RankedSuggestion>& ranked_suggestion) {
        return rank < ranked_suggestion->rank;
      });
}

auto find(AskChannel::RankedSuggestions* suggestions,
          const RankedSuggestion* suggestion) {
  for (auto it = std::lower_bound(
           suggestions->begin(), suggestions->end(), suggestion,
           [](const std::unique_ptr<RankedSuggestion>&a,
              const RankedSuggestion*b) { return a->rank < b->rank; });
       it != suggestions->end(); ++it) {
    // could also bound by upper_bound, but not worth it
    if (it->get() == suggestion) {
      return it;
    }
  }
  FTL_LOG(FATAL) << "RankedSuggestion not found";
  return suggestions->end();
}

void stable_sort(AskChannel::RankedSuggestions* suggestions) {
  std::stable_sort(suggestions->begin(), suggestions->end(),
                   [](const std::unique_ptr<RankedSuggestion>& a,
                      const std::unique_ptr<RankedSuggestion>& b) {
                     return a->rank < b->rank;
                   });
}

}  // namespace

// Ranks based on substring. More complete substrings are ranked better, with a
// secondary rank preferring shorter prefixes.
//
// TODO(rosswang): Allow intersections and more generally edit distance with
// substring discounting.
float AskChannel::Rank(const SuggestionPrototype* prototype) const {
  if (query_.empty()) {
    if (repo_->filter() && !repo_->filter()(*prototype->second->proposal)) {
      return kExcludeRank;
    }
    return next_rank();
  }

  std::string text = prototype->second->proposal->display->headline;
  std::transform(text.begin(), text.end(), text.begin(), ::tolower);
  auto pos = text.find(query_);
  if (pos == std::string::npos)
    return kExcludeRank;

  // major: length by which text exceeds query
  float rank = text.size() - query_.size();
  // minor: match position
  return rank + static_cast<float>(pos) / text.size();
}

RankedSuggestion* AskChannel::OnAddSuggestion(
    const SuggestionPrototype* prototype) {
  const float rank = Rank(prototype);
  if (rank != kExcludeRank) {
    auto& new_entry = *include_.emplace(find_for_insert(&include_, rank),
                                        new RankedSuggestion());
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

void AskChannel::OnChangeSuggestion(RankedSuggestion* ranked_suggestion) {
  // TODO(rosswang): add a change specialization to remove jank
  // When an item is removed and then added using these methods, another item is
  // temporarily shifted in and back out while this goes on. This should not
  // happen.
  const float rank = Rank(ranked_suggestion->prototype);
  if (ranked_suggestion->rank != kExcludeRank) {
    // previously included
    subscriber_.OnRemoveSuggestion(*ranked_suggestion);

    auto from = find(&include_, ranked_suggestion);

    if (rank != kExcludeRank) {
      if (rank != ranked_suggestion->rank) {
        auto to = find_for_insert(&include_, rank);
        if (from < to)
          --to;  // since we're rotating rather than inserting

        if (from != to) {
          if (from < to)
            std::rotate(from, from + 1, to + 1);  // c a b => a b c
          else                                    // if (from > to)
            std::rotate(to, from, from + 1);      // b c a => a b c

          FTL_CHECK(to->get() == ranked_suggestion);
        }  // else keep it stable
        ranked_suggestion->rank = rank;
      }  // else keep it stable

      subscriber_.OnAddSuggestion(*ranked_suggestion);
    } else {
      ranked_suggestion->rank = kExcludeRank;
      exclude_.emplace(ranked_suggestion->prototype->first, std::move(*from));
      include_.erase(from);
    }
  } else {
    // previously excluded
    if (rank != kExcludeRank) {
      auto from = exclude_.find(ranked_suggestion->prototype->first);
      FTL_CHECK(from != exclude_.end());
      ranked_suggestion->rank = rank;
      include_.emplace(find_for_insert(&include_, rank),
                       std::move(from->second));
      exclude_.erase(from);
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

void AskChannel::SetQuery(std::string query) {
  // TODO(rosswang): do we also want to dedup to agents? We almost certainly
  // don't want to pre-normalize, which is kinda contrary with deduping
  auto user_input = UserInput::New();
  user_input->set_text(query);
  repo_->DispatchAsk(std::move(user_input));

  // TODO(rosswang): locale/unicode
  std::transform(query.begin(), query.end(), query.begin(), ::tolower);

  if (query_ != query) {
    query_ = query;

    // Remove included that are now excluded
    auto remove_at = include_.end();
    for (auto it = include_.begin(); it != remove_at;) {
      RankedSuggestion& ranked_suggestion = **it;
      ranked_suggestion.rank = Rank(ranked_suggestion.prototype);

      if (ranked_suggestion.rank == kExcludeRank)
        std::swap(*it, *--remove_at);
      else
        ++it;
    }
    AskChannel::RankedSuggestions to_exclude;
    to_exclude.reserve(include_.end() - remove_at);
    std::move(remove_at, include_.end(), std::back_inserter(to_exclude));
    include_.erase(remove_at, include_.end());

    // Add excluded that are now included
    for (auto it = exclude_.begin(); it != exclude_.end();) {
      RankedSuggestion& ranked_suggestion = *it->second;
      ranked_suggestion.rank = Rank(ranked_suggestion.prototype);
      if (ranked_suggestion.rank != kExcludeRank) {
        include_.emplace_back(std::move(it->second));
        it = exclude_.erase(it);
      } else {
        ++it;
      }
    }

    // Update excluded
    for (auto& ranked_suggestion : to_exclude) {
      exclude_.emplace(ranked_suggestion->prototype->first,
                       std::move(ranked_suggestion));
    }

    stable_sort(&include_);

    subscriber_.Invalidate();
  }
}

}  // namespace maxwell
