// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/fxl/time/time_point.h"
#include "peridot/bin/suggestion_engine/ranked_suggestion.h"

#include <functional>
#include <limits>
#include <string>

namespace maxwell {

// A ranking function sets properties on a |RankedSuggestion| based on the
// |SuggestionPrototype| within the |RankedSuggestion|.
using RankingFunction = std::function<void(RankedSuggestion*)>;

constexpr int64_t kMaxRank = std::numeric_limits<int64_t>::max();

namespace ranking {

RankingFunction GetAskRankingFunction(const std::string& query);
RankingFunction GetNextRankingFunction();

};  // namespace ranking

};  // namespace maxwell
