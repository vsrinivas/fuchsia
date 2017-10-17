// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "peridot/bin/suggestion_engine/query_context.h"
#include "peridot/bin/suggestion_engine/ranked_suggestion.h"

namespace maxwell {

constexpr double kMaxConfidence = 1.0;
constexpr double kMinConfidence = 0.0;

class RankingFeature {
 public:
  RankingFeature();
  virtual ~RankingFeature();

  // Compute the numeric value for a feature, ensuring bounds on the result
  // in the range of [0.0,1.0]
  double ComputeFeature(const QueryContext& query_context,
                        const RankedSuggestion& suggestion);

 protected:
  // Compute the numeric feature for a feature, to be overridden by subclasses
  virtual double ComputeFeatureInternal(const QueryContext& query_context,
                                        const RankedSuggestion& suggestion) = 0;
};

}  // namespace maxwell
