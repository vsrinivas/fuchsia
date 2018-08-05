// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SUGGESTION_ENGINE_FILTERS_RANKED_PASSIVE_FILTER_H_
#define PERIDOT_BIN_SUGGESTION_ENGINE_FILTERS_RANKED_PASSIVE_FILTER_H_

#include <fuchsia/modular/cpp/fidl.h>

#include "peridot/bin/suggestion_engine/filters/suggestion_passive_filter.h"
#include "peridot/bin/suggestion_engine/ranking_features/ranking_feature.h"

namespace modular {

class RankedPassiveFilter : public SuggestionPassiveFilter {
 public:
  RankedPassiveFilter(std::shared_ptr<RankingFeature> ranking_feature);
  ~RankedPassiveFilter() override;

  bool Filter(const std::unique_ptr<RankedSuggestion>& suggestion) override;

 private:
  std::shared_ptr<RankingFeature> ranking_feature_;
};

}  // namespace modular

#endif  // PERIDOT_BIN_SUGGESTION_ENGINE_FILTERS_RANKED_PASSIVE_FILTER_H_
