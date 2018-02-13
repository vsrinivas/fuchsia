// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SUGGESTION_ENGINE_RANKING_FEATURE_H_
#define PERIDOT_BIN_SUGGESTION_ENGINE_RANKING_FEATURE_H_

#include "lib/context/fidl/context_reader.fidl.h"
#include "lib/suggestion/fidl/user_input.fidl.h"

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
  double ComputeFeature(
      const UserInput& query, const RankedSuggestion& suggestion,
      const f1dl::Array<ContextValuePtr>& context_update_values);

  // Fills the context selector with the values and meta the feature needs to
  // request from the context. Returns true if it filled anything, false
  // otherwise.
  ContextSelectorPtr CreateContextSelector();

  // Returns a unique id for the ranking feature instance. This is used to know
  // what context query selector belongs to the ranking feature.
  const std::string UniqueId() const;

 protected:
  // Compute the numeric feature for a feature, to be overridden by subclasses
  virtual double ComputeFeatureInternal(
      const UserInput& query, const RankedSuggestion& suggestion,
      const f1dl::Array<ContextValuePtr>& context_update_values) = 0;

  // Create the context selector. Returns nullptr if the feature doesn't require
  // context.
  virtual ContextSelectorPtr CreateContextSelectorInternal();

 private:
  static int instances_;
  const int id_;
};

}  // namespace maxwell

#endif  // PERIDOT_BIN_SUGGESTION_ENGINE_RANKING_FEATURE_H_
