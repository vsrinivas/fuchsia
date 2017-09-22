// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/ranking.h"

namespace maxwell {

int64_t RankBySubstring(std::string text, const std::string& query) {
  std::transform(text.begin(), text.end(), text.begin(), ::tolower);
  auto pos = text.find(query);
  if (pos == std::string::npos)
    return kMaxRank;

  // major: length by which text exceeds query
  int overlap = text.size() - query.size();
  // minor: match position
  return static_cast<int64_t>(overlap + static_cast<float>(pos) / text.size());
}

int64_t GetDefaultRank(const SuggestionPrototype* prototype) {
  // TODO(andrewosh): Kronk suggestions are downranked for now (low quality).
  if (prototype->source_url.find("kronk") != std::string::npos)
    return kMaxRank;
  return kMaxRank - prototype->timestamp.ToEpochDelta().ToNanoseconds();
}

// TODO(andrewosh): Ross' comment copied from AskChannel.h
//
// Ranks a suggestion prototype. If the suggestion should be included, a //
// meaningful rank is returned. Otherwise, |kExcludeRank| (see *.cc) is
// returned.
//
// Note that these ranks may not be the ones ultimately published to
// subscribers since ambiguous (equal) ranks for an equidistant Rank result
// can lead to nondeterministic UI behavior unless the UI itself implements a
// disambiguator.
//
// TODO(rosswang): This is not the case yet; these ranks may be ambiguous.
// Rather than have complex logic to deal with this at all layers, let's
// revise the interface to side-step this issue.
namespace ranking {

// Ranks based on substring. More complete substrings are ranked better (lower),
// with a secondary rank preferring shorter prefixes.
//
// If a Suggestion is not relevant for a given Ask query (its RankBySubstring
// is kMaxRank, the highest possible rank), then it's instead ranked by
// timestamp.
//
// Since timestamps are much larger than substring ranks, these irrelevant
// suggestions are effectively ranked as a separate partition, after relevant
// suggestions.
//
// TODO(rosswang): Allow intersections and more generally edit distance with
// substring discounting.
RankingFunction GetAskRankingFunction(const std::string& query) {
  return [query = std::move(query)](const SuggestionPrototype* prototype)
      ->int64_t {
    if (query.empty()) {
      return GetDefaultRank(prototype);
    }

    const auto& display = prototype->proposal->display;
    const int64_t substring_rank =
        std::min(RankBySubstring(display->headline, query),
                 std::min(RankBySubstring(display->subheadline, query),
                          RankBySubstring(display->details, query)));
    if (substring_rank == kMaxRank) {
      return GetDefaultRank(prototype);
    }
    return substring_rank;
  };
}

RankingFunction GetNextRankingFunction() {
  return [](const SuggestionPrototype* prototype) -> int64_t {
    return GetDefaultRank(prototype);
  };
}

};  // namespace ranking

};  // namespace maxwell
