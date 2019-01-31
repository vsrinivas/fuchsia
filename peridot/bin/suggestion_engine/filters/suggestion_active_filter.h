// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SUGGESTION_ENGINE_FILTERS_SUGGESTION_ACTIVE_FILTER_H_
#define PERIDOT_BIN_SUGGESTION_ENGINE_FILTERS_SUGGESTION_ACTIVE_FILTER_H_

#include <vector>

#include "peridot/bin/suggestion_engine/ranked_suggestion.h"

namespace modular {

class SuggestionActiveFilter {
 public:
  SuggestionActiveFilter();
  virtual ~SuggestionActiveFilter();

  // Browse through the vector of ranked suggestions and remove/alter the
  // suggestions.
  virtual void Filter(
      std::vector<std::unique_ptr<RankedSuggestion>>* const suggestions) = 0;
};

}  // namespace modular

#endif  // PERIDOT_BIN_SUGGESTION_ENGINE_FILTERS_SUGGESTION_ACTIVE_FILTER_H_
