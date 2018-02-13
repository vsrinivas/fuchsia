// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/ranking_feature.h"

#include "lib/fxl/logging.h"

namespace maxwell {

int RankingFeature::instances_ = 0;

RankingFeature::RankingFeature() : id_(instances_++) {};

RankingFeature::~RankingFeature() = default;

double RankingFeature::ComputeFeature(
    const UserInput& query, const RankedSuggestion& suggestion,
    const f1dl::Array<ContextValuePtr>& context_update_values) {
  const double feature = ComputeFeatureInternal(
      query, suggestion, context_update_values);
  FXL_CHECK(feature <= kMaxConfidence);
  FXL_CHECK(feature >= kMinConfidence);
  return feature;
}

ContextSelectorPtr RankingFeature::CreateContextSelector() {
  return CreateContextSelectorInternal();
}

const std::string RankingFeature::UniqueId() const {
  std::stringstream ss;
  ss << "rf_" << id_;
  return ss.str();
}

ContextSelectorPtr RankingFeature::CreateContextSelectorInternal() {
  // By default we return a nullptr, meaning that the ranking feature doesn't
  // require context. If a ranking feature requires context, it should create a
  // context selector, set the values it needs and return it.
  return nullptr;
}

}  // namespace maxwell
