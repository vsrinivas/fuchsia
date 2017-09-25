// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/fxl/time/time_point.h"
#include "peridot/bin/suggestion_engine/suggestion_prototype.h"

#include <functional>
#include <limits>
#include <string>

namespace maxwell {

using RankingFunction = std::function<int64_t(const SuggestionPrototype*)>;

constexpr int64_t kMaxRank = std::numeric_limits<int64_t>::max();

int64_t RankBySubstring(std::string text, const std::string& query);
int64_t GetDefaultRank(const SuggestionPrototype* prototype);

namespace ranking {

RankingFunction GetAskRankingFunction(const std::string& query);
RankingFunction GetNextRankingFunction();

};  // namespace ranking

};  // namespace maxwell
