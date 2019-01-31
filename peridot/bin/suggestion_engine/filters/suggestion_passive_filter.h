// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SUGGESTION_ENGINE_FILTERS_SUGGESTION_PASSIVE_FILTER_H_
#define PERIDOT_BIN_SUGGESTION_ENGINE_FILTERS_SUGGESTION_PASSIVE_FILTER_H_

#include "peridot/bin/suggestion_engine/ranked_suggestion.h"

namespace modular {

class SuggestionPassiveFilter {
 public:
  SuggestionPassiveFilter();
  virtual ~SuggestionPassiveFilter();

  // Evaluate the suggestion based on the filter criteria and return a boolean
  // value.
  virtual bool Filter(const std::unique_ptr<RankedSuggestion>& suggestion) = 0;
};

}  // namespace modular

#endif  // PERIDOT_BIN_SUGGESTION_ENGINE_FILTERS_SUGGESTION_PASSIVE_FILTER_H_
