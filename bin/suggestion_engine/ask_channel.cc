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
// rank offset for direct suggestions, which are always ranked before inherited
// suggestions
constexpr float kDirectOffset = -10000;

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
  FTL_LOG(FATAL) << "RankedSuggestion with proposal ID "
                 << suggestion->prototype->proposal->id << " at rank "
                 << suggestion->rank << " not found";
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

AskChannel::~AskChannel() {
  // clean up all rank_by_channel entries for this channel
  for (auto& ranked_suggestion : include_) {
    ranked_suggestion->prototype->ranks_by_channel.erase(this);
  }
  for (auto& exclude : exclude_) {
    exclude.second->prototype->ranks_by_channel.erase(this);
  }
}

float RankBySubstring(std::string text, const std::string& query) {
  std::transform(text.begin(), text.end(), text.begin(), ::tolower);
  auto pos = text.find(query);
  if (pos == std::string::npos)
    return kExcludeRank;

  // major: length by which text exceeds query
  float rank = text.size() - query.size();
  // minor: match position
  return rank + static_cast<float>(pos) / text.size();
}

// Ranks based on substring. More complete substrings are ranked better (lower),
// with a secondary rank preferring shorter prefixes.
//
// TODO(rosswang): Allow intersections and more generally edit distance with
// substring discounting.
float AskChannel::Rank(const SuggestionPrototype* prototype) {
  const auto& source_direct_ids = direct_proposal_ids_.find(prototype->source);
  if (source_direct_ids != direct_proposal_ids_.end() &&
      source_direct_ids->second.find(prototype->proposal->id) !=
          source_direct_ids->second.end()) {
    return next_rank() + kDirectOffset;
  }

  if (query_.empty()) {
    if (repo_->filter() && !repo_->filter()(*prototype->proposal)) {
      return kExcludeRank;
    }
    return next_rank();
  }

  const auto& display = prototype->proposal->display;
  return std::min(RankBySubstring(display->headline, query_),
                  std::min(RankBySubstring(display->subheadline, query_),
                           RankBySubstring(display->details, query_)));
}

void AskChannel::OnAddSuggestion(SuggestionPrototype* prototype) {
  const float rank = Rank(prototype);
  if (rank != kExcludeRank) {
    auto& new_entry = *include_.emplace(find_for_insert(&include_, rank),
                                        new RankedSuggestion());
    new_entry->rank = rank;
    new_entry->prototype = prototype;

    subscriber_.OnAddSuggestion(*new_entry);
    debug_->OnAskStart(query_, ranked_suggestions());

    prototype->ranks_by_channel[this] = new_entry.get();
  } else {
    auto& new_entry =
        exclude_.emplace(prototype->suggestion_id, new RankedSuggestion())
            .first->second;
    new_entry->rank = kExcludeRank;
    new_entry->prototype = prototype;

    prototype->ranks_by_channel[this] = new_entry.get();
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
    debug_->OnAskStart(query_, ranked_suggestions());

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
      debug_->OnAskStart(query_, ranked_suggestions());
    } else {
      ranked_suggestion->rank = kExcludeRank;
      exclude_.emplace(ranked_suggestion->prototype->suggestion_id,
                       std::move(*from));
      include_.erase(from);
    }
  } else {
    // previously excluded
    if (rank != kExcludeRank) {
      auto from = exclude_.find(ranked_suggestion->prototype->suggestion_id);
      FTL_CHECK(from != exclude_.end());
      ranked_suggestion->rank = rank;
      include_.emplace(find_for_insert(&include_, rank),
                       std::move(from->second));
      exclude_.erase(from);
      subscriber_.OnAddSuggestion(*ranked_suggestion);
      debug_->OnAskStart(query_, ranked_suggestions());
    }
    // else no action required
  }
}

void AskChannel::OnRemoveSuggestion(const RankedSuggestion* ranked_suggestion) {
  // Note that exlude_/include_.erase invalidates ranked_suggestion, so the
  // ranks_by_channel.erase must happen first.
  if (ranked_suggestion->rank == kExcludeRank) {
    ranked_suggestion->prototype->ranks_by_channel.erase(this);
    exclude_.erase(ranked_suggestion->prototype->suggestion_id);
  } else {
    subscriber_.OnRemoveSuggestion(*ranked_suggestion);
    ranked_suggestion->prototype->ranks_by_channel.erase(this);
    include_.erase(find(&include_, ranked_suggestion));
  }
}

void AskChannel::DirectProposal(ProposalPublisherImpl* publisher,
                                fidl::Array<ProposalPtr> proposals) {
  std::unordered_set<std::string>& ids_to_include =
      direct_proposal_ids_[publisher];
  std::unordered_set<std::string> ids_to_remove;
  std::swap(ids_to_remove, ids_to_include);

  for (auto& proposal : proposals) {
    ids_to_remove.erase(proposal->id);
    ids_to_include.insert(proposal->id);
    publisher->Propose(std::move(proposal), this);
  }

  for (const auto& id_to_remove : ids_to_remove) {
    publisher->Remove(id_to_remove, this);
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
  repo_->DispatchAsk(std::move(user_input), this);

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
      exclude_.emplace(ranked_suggestion->prototype->suggestion_id,
                       std::move(ranked_suggestion));
    }

    stable_sort(&include_);

    // TODO(rosswang): Depending on the query/proposal agents, this might be
    // unnecessarily drastic.
    subscriber_.Invalidate();
  }
}

}  // namespace maxwell
