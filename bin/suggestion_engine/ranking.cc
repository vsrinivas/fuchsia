// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/ranking.h"

namespace maxwell {

namespace {

// This is a stand-in for an "agent usefulness" metric for the Kronk agent.
constexpr float kKronkHandicap = .9f;
constexpr float kDefaultConfidence = .7f;

// TODO(jwnichols): dead code
/*int64_t RankBySubstring(std::string text, const std::string& query) {
  std::transform(text.begin(), text.end(), text.begin(), ::tolower);
  auto pos = text.find(query);
  if (pos == std::string::npos)
    return kMaxRank;

  // major: length by which text exceeds query
  int overlap = text.size() - query.size();
  // minor: match position
  return static_cast<int64_t>(overlap + static_cast<float>(pos) / text.size());
}*/

void DefaultRank(RankedSuggestion* to_rank) {
  to_rank->adjusted_confidence = to_rank->prototype->proposal->confidence;

  // TODO(andrewosh): Kronk suggestions are downranked for now (low quality).
  if (to_rank->prototype->source_url.find("kronk") != std::string::npos) {
    to_rank->adjusted_confidence *= kKronkHandicap;
  } else if (to_rank->adjusted_confidence == 0) {
    // no hint given
    to_rank->adjusted_confidence = kDefaultConfidence;
  }

  to_rank->rank = (1 - to_rank->adjusted_confidence) * kMaxRank;
}

}  // namespace

namespace ranking {

// TODO(rosswang): use the default ranking for now
RankingFunction GetAskRankingFunction(const std::string& query) {
  return DefaultRank;
}

RankingFunction GetNextRankingFunction() {
  return DefaultRank;
}

};  // namespace ranking

};  // namespace maxwell
